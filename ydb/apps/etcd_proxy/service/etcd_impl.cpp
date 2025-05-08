#include "etcd_impl.h"
#include "etcd_shared.h"
#include "etcd_oper.h"

#include <ydb/apps/etcd_proxy/proto/rpc.grpc.pb.h>

#include <ydb/core/grpc_services/rpc_scheme_base.h>

#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/query/tx.h>

#include <ydb/library/actors/core/executor_thread.h>

namespace NEtcd {

using namespace NYdb::NQuery;
using namespace NActors;

namespace {

class TBaseEtcdRequest {
protected:
    virtual std::string ParseGrpcRequest() = 0;
    virtual void MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder& params) = 0;
    virtual void ReplyWith(const NYdb::TResultSets& results, const TActorContext& ctx) = 0;
    virtual bool ExecuteAsync() const { return false; }
    virtual bool RequiredNextRevision() const { return false; }
    virtual TKeysSet GetAffectedKeysSet() const { return {}; }

    i64 Revision = 0LL;
};

using namespace NKikimr::NGRpcService;

template <typename TDerived, typename TRequest, bool ReadOnly = false>
class TEtcdRequestGrpc
    : public TActorBootstrapped<TEtcdRequestGrpc<TDerived, TRequest, ReadOnly>>
    , public TBaseEtcdRequest
{
    friend class TBaseEtcdRequest;
public:
    TEtcdRequestGrpc(std::unique_ptr<NKikimr::NGRpcService::IRequestCtx> request, TSharedStuff::TPtr stuff)
        : Request_(std::move(request)), Stuff(std::move(stuff))
    {}

    void Bootstrap(const TActorContext& ctx) {
        if (const auto& error = this->ParseGrpcRequest(); !error.empty()) {
            this->Request_->ReplyWithRpcStatus(grpc::StatusCode::INVALID_ARGUMENT, TString(error));
            this->Die(ctx);
        } else if (this->RequiredNextRevision()) {
            this->Become(&TEtcdRequestGrpc::WaitRevisionFunc);
            ctx.Send(Stuff->MainGate, new TEvRequestRevision(this->GetAffectedKeysSet()));
        } else {
            SendDatabaseRequest(ctx);
        }
    }
private:
    static std::string_view GetRequestName() {
        return TRequest::TRequest::descriptor()->name();
    }

    virtual std::ostream& Dump(std::ostream& out) const = 0;

    TQueryClient::TQueryResultFunc GetQueryResultFunc() {
        NYdb::TParamsBuilder params;
        if (this->RequiredNextRevision())
            AddParam("Revision", params, Revision);
        std::ostringstream sql;
        sql << Stuff->TablePrefix;
        sql << "-- " << GetRequestName() << " >>>>" << std::endl;
        this->MakeQueryWithParams(sql, params);
        if (this->RequiredNextRevision())
            sql << "insert into `commited` (`revision`,`timestamp`) values ($Revision,CurrentUtcDatetime());" << std::endl;
        else
            sql << "select nvl(max(`revision`), 0L) from `commited`;" << std::endl;
        sql << "-- " << GetRequestName() << " <<<<" << std::endl;
//      std::cout << std::endl << sql.view() << std::endl;

        return [query = sql.str(), args = params.Build()](TQueryClient::TSession session) -> TAsyncExecuteQueryResult {
            return session.ExecuteQuery(query, TTxControl::BeginTx().CommitTx(), args);
        };
    }

    void SendDatabaseRequest(const TActorContext& ctx) {
        this->Become(&TEtcdRequestGrpc::WaitResultFunc);

        if (this->ExecuteAsync()) {
            Stuff->Client->RetryQuery(GetQueryResultFunc()).Subscribe([name = GetRequestName()](const auto& future) {
                if (const auto res = future.GetValue(); res.IsSuccess())
                    std::cout << name << " finished succesfully." << std::endl;
                else
                    std::cout << name << " finished with errors: " << res.GetIssues().ToString() << std::endl;
            });
            ctx.Send(this->SelfId(), new TEvQueryResult);
        } else {
            Stuff->Client->RetryQuery(GetQueryResultFunc()).Subscribe([my = this->SelfId(), stuff = TSharedStuff::TWeakPtr(Stuff)](const auto& future) {
                if (const auto lock = stuff.lock()) {
                    if (const auto res = future.GetValue(); res.IsSuccess())
                        lock->ActorSystem->Send(my, new TEvQueryResult(res.GetResultSets()));
                    else
                        lock->ActorSystem->Send(my, new TEvQueryError(res.GetIssues()));
                }
            });
        }
    }

    STFUNC(WaitRevisionFunc) {
        switch (ev->GetTypeRewrite()) {
            HFunc(TEvReturnRevision, Handle);
            HFunc(TEvQueryError, Handle);
        }
    }

    STFUNC(WaitResultFunc) {
        switch (ev->GetTypeRewrite()) {
            HFunc(TEvQueryResult, Handle);
            HFunc(TEvQueryError, Handle);
        }
    }

    void Handle(TEvReturnRevision::TPtr &ev, const TActorContext& ctx) {
        Revision = ev->Get()->Revision;
        SendDatabaseRequest(ctx);
    }

    void Handle(TEvQueryResult::TPtr &ev, const TActorContext& ctx) {
        if (!this->RequiredNextRevision()) {
            if (auto parser = NYdb::TResultSetParser(ev->Get()->Results.back()); parser.TryNextRow()) {
                Revision = NYdb::TValueParser(parser.GetValue(0)).GetInt64();
            }
        }
        ReplyWith(ev->Get()->Results, ctx);
    }

    void Handle(TEvQueryError::TPtr &ev, const TActorContext& ctx) {
        TryToRollbackRevision();
        std::ostringstream err;
        Dump(err) << " SQL error received:" << std::endl << ev->Get()->Issues.ToString() << std::endl;
        std::cout << err.view();
        Reply(grpc::StatusCode::INTERNAL, err.view(), ctx);
    }
protected:
    void TryToRollbackRevision() {
        if constexpr (!ReadOnly) {
            Stuff->Revision.compare_exchange_weak(Revision, Revision - 1LL);
        }
    }

    const typename TRequest::TRequest* GetProtoRequest() const {
        return TRequest::GetProtoRequest(Request_);
    }

    void Reply(typename TRequest::TResponse& resp, const TActorContext& ctx) {
        this->Request_->Reply(&resp);
        this->Die(ctx);
    }

    void Reply(grpc::StatusCode code, const std::string_view& error, const TActorContext& ctx) {
        this->Request_->ReplyWithRpcStatus(code, TString(error));
        this->Die(ctx);
    }

    const std::unique_ptr<NKikimr::NGRpcService::IRequestCtx> Request_;
    const TSharedStuff::TPtr Stuff;
};

using TEvRangeKVRequest = TGrpcRequestNoOperationCall<etcdserverpb::RangeRequest, etcdserverpb::RangeResponse>;
using TEvPutKVRequest = TGrpcRequestNoOperationCall<etcdserverpb::PutRequest, etcdserverpb::PutResponse>;
using TEvDeleteRangeKVRequest = TGrpcRequestNoOperationCall<etcdserverpb::DeleteRangeRequest, etcdserverpb::DeleteRangeResponse>;
using TEvTxnKVRequest = TGrpcRequestNoOperationCall<etcdserverpb::TxnRequest, etcdserverpb::TxnResponse>;
using TEvCompactKVRequest = TGrpcRequestNoOperationCall<etcdserverpb::CompactionRequest, etcdserverpb::CompactionResponse>;

using TEvLeaseGrantRequest = TGrpcRequestNoOperationCall<etcdserverpb::LeaseGrantRequest, etcdserverpb::LeaseGrantResponse>;
using TEvLeaseRevokeRequest = TGrpcRequestNoOperationCall<etcdserverpb::LeaseRevokeRequest, etcdserverpb::LeaseRevokeResponse>;
using TEvLeaseTimeToLiveRequest = TGrpcRequestNoOperationCall<etcdserverpb::LeaseTimeToLiveRequest, etcdserverpb::LeaseTimeToLiveResponse>;
using TEvLeaseLeasesRequest = TGrpcRequestNoOperationCall<etcdserverpb::LeaseLeasesRequest, etcdserverpb::LeaseLeasesResponse>;

class TRangeRequest
    : public TEtcdRequestGrpc<TRangeRequest, TEvRangeKVRequest, true> {
public:
    using TBase = TEtcdRequestGrpc<TRangeRequest, TEvRangeKVRequest, true>;
    using TBase::TBase;
private:
    std::string ParseGrpcRequest() final {
        Revision = Stuff->Revision.load();
        return Range.Parse(*GetProtoRequest());
    }

    void MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder& params) final {
        return Range.MakeQueryWithParams(sql, params);
    }

    void ReplyWith(const NYdb::TResultSets& results, const TActorContext& ctx) final {
        auto response = Range.MakeResponse(Revision, results);
        Dump(std::cout) << '=' << response.count() << std::endl;
        return Reply(response, ctx);
    }

    std::ostream& Dump(std::ostream& out) const final {
        return Range.Dump(out);
    }

    TRange Range;
};

class TPutRequest
    : public TEtcdRequestGrpc<TPutRequest, TEvPutKVRequest> {
public:
    using TBase = TEtcdRequestGrpc<TPutRequest, TEvPutKVRequest>;
    using TBase::TBase;
private:
    std::string ParseGrpcRequest() final {
        if (const auto& error = Put.Parse(*GetProtoRequest()); !error.empty())
            return error;

        Revision = Stuff->Revision.fetch_add(1LL) + 1LL;
        return {};
    }

    void MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder& params) final {
        return Put.MakeQueryWithParams(sql, params);
    }

    void ReplyWith(const NYdb::TResultSets& results, const TActorContext& ctx) final {
        const auto watcher = Stuff->Watchtower;
        const auto notifier = [&watcher, &ctx](std::string&& key, i64 revision, TData&& oldData, TData&& newData) {
            ctx.Send(watcher, std::make_unique<TEvChange>(std::move(key), revision, std::move(oldData), std::move(newData)));
        };

        auto response = Put.MakeResponse(Revision, results, notifier);
        Dump(std::cout) << '=';
        if (const auto good = std::get_if<etcdserverpb::PutResponse>(&response)) {
            std::cout << "ok" << std::endl;
            return Reply(*good, ctx);
        } else if (const auto bad = std::get_if<TGrpcError>(&response)) {
            TryToRollbackRevision();
            std::cout << bad->second << std::endl;
            return Reply(bad->first, bad->second, ctx);
        }
    }

    std::ostream& Dump(std::ostream& out) const final {
        return Put.Dump(out);
    }

    bool RequiredNextRevision() const final {
        return true;
    }

    TPut Put;
};

class TDeleteRangeRequest
    : public TEtcdRequestGrpc<TDeleteRangeRequest, TEvDeleteRangeKVRequest> {
public:
    using TBase = TEtcdRequestGrpc<TDeleteRangeRequest, TEvDeleteRangeKVRequest>;
    using TBase::TBase;
private:
    std::string ParseGrpcRequest() final {
        if (const auto& error = DeleteRange.Parse(*GetProtoRequest()); !error.empty())
            return error;
        Revision = Stuff->Revision.fetch_add(1LL) + 1LL;
        return {};
    }

    void MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder& params) final {
        return DeleteRange.MakeQueryWithParams(sql, params);
    }

    void ReplyWith(const NYdb::TResultSets& results, const TActorContext& ctx) final {
        const auto watcher = Stuff->Watchtower;
        const auto notifier = [&watcher, &ctx](std::string&& key, i64 revision, TData&& oldData, TData&& newData) {
            ctx.Send(watcher, std::make_unique<TEvChange>(std::move(key), revision, std::move(oldData), std::move(newData)));
        };

        auto response = DeleteRange.MakeResponse(Revision, results, notifier);
        if (!response.deleted())
            TryToRollbackRevision();

        Dump(std::cout) << '=' << response.deleted() << std::endl;
        return Reply(response, ctx);
    }

    std::ostream& Dump(std::ostream& out) const final {
        return DeleteRange.Dump(out);
    }

    bool RequiredNextRevision() const final {
        return true;
    }

    TDeleteRange DeleteRange;
};

class TTxnRequest
    : public TEtcdRequestGrpc<TTxnRequest, TEvTxnKVRequest> {
public:
    using TBase = TEtcdRequestGrpc<TTxnRequest, TEvTxnKVRequest>;
    using TBase::TBase;
private:
    std::string ParseGrpcRequest() final {
        if (const auto& error = Txn.Parse(*GetProtoRequest()); !error.empty())
            return error;
        Revision = Stuff->Revision.fetch_add(1LL) + 1LL;
        return {};
    }

    void MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder& params) final {
        TKeysSet keys;
        Txn.GetKeys(keys);
        size_t resultsCounter = 0U, paramsCounter = 0U;
        if (keys.empty()) {
            return Txn.MakeQueryWithParams(sql, {}, true, {}, params, &resultsCounter, &paramsCounter);
        } else if (1U == keys.size()) {
            std::ostringstream where;
            where << " where ";
            const auto& keyParamName = MakeSimplePredicate(keys.cbegin()->first, keys.cbegin()->second, where, params);
            return Txn.MakeQueryWithParams(sql, keyParamName, keys.cbegin()->second.empty(), where.view(), params, &resultsCounter, &paramsCounter);
        };

        return Txn.MakeQueryWithParams(sql, params, &resultsCounter, &paramsCounter);
    }

    void ReplyWith(const NYdb::TResultSets& results, const TActorContext& ctx) final {
        const auto watcher = Stuff->Watchtower;
        const auto notifier = [&watcher, &ctx](std::string&& key, i64 revision, TData&& oldData, TData&& newData) {
            ctx.Send(watcher, std::make_unique<TEvChange>(std::move(key), revision, std::move(oldData), std::move(newData)));
        };

        auto response = Txn.MakeResponse(Revision, results, notifier);
        Dump(std::cout) << '=';
        if (const auto good = std::get_if<etcdserverpb::TxnResponse>(&response)) {
            std::cout << (good->succeeded() ? "success" : "failure") << std::endl;
            return Reply(*good, ctx);
        } else if (const auto bad = std::get_if<TGrpcError>(&response)) {
            TryToRollbackRevision();
            std::cout << bad->second << std::endl;
            return Reply(bad->first, bad->second, ctx);
        }
    }

    std::ostream& Dump(std::ostream& out) const final {
        return Txn.Dump(out);
    }

    bool RequiredNextRevision() const final {
        return !Txn.IsReadOnly();
    }

    TTxn Txn;
};

class TCompactRequest
    : public TEtcdRequestGrpc<TCompactRequest, TEvCompactKVRequest> {
public:
    using TBase = TEtcdRequestGrpc<TCompactRequest, TEvCompactKVRequest>;
    using TBase::TBase;
private:
    std::string ParseGrpcRequest() final {
        Revision = Stuff->Revision.load();

        const auto &rec = *GetProtoRequest();
        KeyRevision = rec.revision();
        Physical = rec.physical();
        if (KeyRevision <= 0LL | KeyRevision >= Revision)
            return std::string("invalid revision:" ) += std::to_string(KeyRevision);
        return {};
    }

    bool ExecuteAsync() const final {
        return !Physical;
    }

    void MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder& params) final {
        const auto& revisionParamName = AddParam("Revision", params, KeyRevision);
        sql << "$Trash = select c.key as key, c.modified as modified from `history` as c inner join (" << std::endl;
        sql << "select max_by((`key`, `modified`), `modified`) as pair from `history`" << std::endl;
        sql << "where `modified` < " << revisionParamName << " and 0L = `version` group by `key`" << std::endl;
        sql << ") as keys on keys.pair.0 = c.key where c.modified <= keys.pair.1;" << std::endl;
        sql << "delete from `history` on select * from $Trash;" << std::endl;
        sql << "delete from `commited` where " << revisionParamName << " >= `revision`;" << std::endl;
        if (Physical) {
            sql << "select count(*) from $Trash;" << std::endl;
        }
    }

    void ReplyWith(const NYdb::TResultSets& results, const TActorContext& ctx) final {
        Dump(std::cout);
        if (Physical) {
            auto parser = NYdb::TResultSetParser(results.front());
            const auto erased = parser.TryNextRow() ? NYdb::TValueParser(parser.GetValue(0)).GetUint64() : 0ULL;
            if (!erased)
                TryToRollbackRevision();

            std::cout << '=' << erased << std::endl;
        } else {
            std::cout << " is executing asynchronously." << std::endl;
        }

        etcdserverpb::CompactionResponse response;
        response.mutable_header()->set_revision(Revision);
        return Reply(response, ctx);
    }

    std::ostream& Dump(std::ostream& out) const final {
        out << "Compact(" << KeyRevision;
        if (Physical)
            out << ",physical";
        return out << ')';
    }

    i64 KeyRevision;
    bool Physical;
};

class TLeaseGrantRequest
    : public TEtcdRequestGrpc<TLeaseGrantRequest, TEvLeaseGrantRequest> {
public:
    using TBase = TEtcdRequestGrpc<TLeaseGrantRequest, TEvLeaseGrantRequest>;
    using TBase::TBase;
private:
    std::string ParseGrpcRequest() final {
        Revision = Stuff->Revision.load();
        const auto &rec = *GetProtoRequest();
        TTL = rec.ttl();

        if (rec.id())
            return "requested id isn't supported";

        Lease = Stuff->Lease.fetch_add(1LL) + 1LL;
        return {};
    }

    void MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder& params) final {
        sql << "insert into `leases` (`id`,`ttl`,`created`,`updated`)" << std::endl;
        sql << '\t' << "values (" << AddParam("Lease", params, Lease) << ',' << AddParam("TimeToLive", params, TTL) << ",CurrentUtcDatetime(),CurrentUtcDatetime());" << std::endl;
    }

    void ReplyWith(const NYdb::TResultSets&, const TActorContext& ctx) final {
        etcdserverpb::LeaseGrantResponse response;
        response.mutable_header()->set_revision(Revision);
        response.set_id(Lease);
        response.set_ttl(TTL);
        Dump(std::cout) << '=' << response.id() << ',' << response.ttl() << std::endl;
        return Reply(response, ctx);
    }

    std::ostream& Dump(std::ostream& out) const final {
        return out << "Grant(" << TTL << ')';
    }

    i64 Lease, TTL;
};

class TLeaseRevokeRequest
    : public TEtcdRequestGrpc<TLeaseRevokeRequest, TEvLeaseRevokeRequest> {
public:
    using TBase = TEtcdRequestGrpc<TLeaseRevokeRequest, TEvLeaseRevokeRequest>;
    using TBase::TBase;
private:
    std::string ParseGrpcRequest() final {
        const auto &rec = *GetProtoRequest();
        Lease = rec.id();

        if (!Lease)
            return "lease id isn't set";

        Revision = Stuff->Revision.fetch_add(1LL) + 1LL;
        return {};
    }

    void MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder& params) final {
        const auto& revisionParamName = AddParam("Revision", params, Revision);
        const auto& leaseParamName = AddParam("Lease", params, Lease);

        sql << "select count(*) > 0UL from `leases` where " << leaseParamName << " = `id`;" << std::endl;

        sql << "$Victims = select `key`, `value`, `created`, `modified`, `version`, `lease` from `current` view `lease` where " << leaseParamName << " = `lease`;" << std::endl;

        if constexpr (NotifyWatchtower) {
            sql << "select `key`, `value`, `created`, `modified`, `version`, `lease` from $Victims;" << std::endl;
        }

        sql << "insert into `history`" << std::endl;
        sql << "select `key`, `created`, " << revisionParamName << " as `modified`, 0L as `version`, `value`, `lease` from $Victims;" << std::endl;
        sql << "delete from `current` on select `key` from $Victims;" << std::endl;
        sql << "delete from `leases` where " << leaseParamName << " = `id`;" << std::endl;
    }

    void ReplyWith(const NYdb::TResultSets& results, const TActorContext& ctx) final {
        if (auto parser = NYdb::TResultSetParser(results.front()); parser.TryNextRow()) {
            if (!NYdb::TValueParser(parser.GetValue(0)).GetBool()) {
                TryToRollbackRevision();
                return Reply(grpc::StatusCode::NOT_FOUND, "requested lease not found", ctx);
            }
        }

        if constexpr (NotifyWatchtower) {
            i64 deleted = 0ULL;
            for (auto parser = NYdb::TResultSetParser(results[1U]); parser.TryNextRow(); ++deleted) {
                TData oldData;
                oldData.Value = NYdb::TValueParser(parser.GetValue("value")).GetString();
                oldData.Created = NYdb::TValueParser(parser.GetValue("created")).GetInt64();
                oldData.Modified = NYdb::TValueParser(parser.GetValue("modified")).GetInt64();
                oldData.Version = NYdb::TValueParser(parser.GetValue("version")).GetInt64();
                oldData.Lease = NYdb::TValueParser(parser.GetValue("lease")).GetInt64();
                auto key = NYdb::TValueParser(parser.GetValue("key")).GetString();

                ctx.Send(Stuff->Watchtower, std::make_unique<TEvChange>(std::move(key), Revision, std::move(oldData)));
            }

            if (!deleted)
                TryToRollbackRevision();
        }

        etcdserverpb::LeaseRevokeResponse response;
        response.mutable_header()->set_revision(Revision);
        Dump(std::cout) << std::endl;
        return Reply(response, ctx);
    }

    std::ostream& Dump(std::ostream& out) const final {
        return out << "Revoke(" << Lease << ')';
    }

    i64 Lease;
};

class TLeaseTimeToLiveRequest
    : public TEtcdRequestGrpc<TLeaseTimeToLiveRequest, TEvLeaseTimeToLiveRequest> {
public:
    using TBase = TEtcdRequestGrpc<TLeaseTimeToLiveRequest, TEvLeaseTimeToLiveRequest>;
    using TBase::TBase;
private:
    std::string ParseGrpcRequest() final {
        Revision = Stuff->Revision.load();

        const auto &rec = *GetProtoRequest();
        Lease = rec.id();
        Keys = rec.keys();
        if (!Lease)
            return "lease id isn't set";
        return {};
    }

    void MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder& params) final {
        const auto& leaseParamName = AddParam("Lease", params, Lease);

        sql << "select `ttl`, `ttl` - unwrap(cast(CurrentUtcDatetime(`id`) - `updated` as Int64) / 1000000L) as `granted` from `leases` where " << leaseParamName << " = `id`;" << std::endl;
        if (Keys) {
            sql << "select `key` from `current` where " << leaseParamName << " = `lease`;" << std::endl;
        }
    }

    void ReplyWith(const NYdb::TResultSets& results, const TActorContext& ctx) final {
        etcdserverpb::LeaseTimeToLiveResponse response;
        response.mutable_header()->set_revision(Revision);

        response.set_id(Lease);
        auto parser = NYdb::TResultSetParser(results.front());
        const bool exists = parser.TryNextRow();
        response.set_ttl(exists ? NYdb::TValueParser(parser.GetValue("ttl")).GetInt64() : -1LL);
        response.set_grantedttl(exists ? NYdb::TValueParser(parser.GetValue("granted")).GetInt64() : 0LL);

        if (Keys) {
            for (auto parser = NYdb::TResultSetParser(results[1U]); parser.TryNextRow();) {
                response.add_keys(NYdb::TValueParser(parser.GetValue(0)).GetString());
            }
        }

        Dump(std::cout) << '=' << response.ttl() << ',' << response.grantedttl() << std::endl;
        return Reply(response, ctx);
    }

    std::ostream& Dump(std::ostream& out) const final {
        return out << "TimeToLive(" << Lease << ')';
    }

    i64 Lease = 0LL;
    bool Keys = false;
};

class TLeaseLeasesRequest
    : public TEtcdRequestGrpc<TLeaseLeasesRequest, TEvLeaseLeasesRequest> {
public:
    using TBase = TEtcdRequestGrpc<TLeaseLeasesRequest, TEvLeaseLeasesRequest>;
    using TBase::TBase;
private:
    std::string ParseGrpcRequest() final {
        Revision = Stuff->Revision.load();
        return {};
    }

    void MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder&) final {
        sql << "select `id` from `leases`;" << std::endl;
    }

    void ReplyWith(const NYdb::TResultSets& results, const TActorContext& ctx) final {
        etcdserverpb::LeaseLeasesResponse response;
        response.mutable_header()->set_revision(Revision);

        for (auto parser = NYdb::TResultSetParser(results.front()); parser.TryNextRow();) {
            response.add_leases()->set_id(NYdb::TValueParser(parser.GetValue(0)).GetInt64());
        }

        Dump(std::cout) << '=' << response.leases().size() << std::endl;
        return Reply(response, ctx);
    }

    std::ostream& Dump(std::ostream& out) const final {
        return out << "Leases()";
    }
};

}

std::string GetParamName(const std::string_view& name, size_t* counter) {
    auto param = std::string(1U, '$') += name;
    if (counter)
        param += std::to_string((*counter)++);
    return param;
}

template<typename TValueType>
std::string AddParam(const std::string_view& name, NYdb::TParamsBuilder& params, const TValueType& value, size_t* counter) {
    const auto param = GetParamName(name, counter);
    if constexpr (std::is_same<TValueType, std::string_view>::value) {
        params.AddParam(param).String(std::string(value)).Build();
    } else if constexpr (std::is_same<TValueType, std::string>::value) {
        params.AddParam(param).String(value).Build();
    } else if constexpr (std::is_same<TValueType, i64>::value) {
        if (!value)
            return "0L";
        params.AddParam(param).Int64(value).Build();
    } else if constexpr (std::is_same<TValueType, ui64>::value) {
        if (!value)
            return "0UL";
        params.AddParam(param).Uint64(value).Build();
    }
    return param;
}

template std::string AddParam<i64>(const std::string_view& name, NYdb::TParamsBuilder& params, const i64& value, size_t* counter);
template std::string AddParam<ui64>(const std::string_view& name, NYdb::TParamsBuilder& params, const ui64& value, size_t* counter);
template std::string AddParam<std::string>(const std::string_view& name, NYdb::TParamsBuilder& params, const std::string& value, size_t* counter);
template std::string AddParam<std::string_view>(const std::string_view& name, NYdb::TParamsBuilder& params, const std::string_view& value, size_t* counter);

std::string MakeSimplePredicate(const std::string_view& key, const std::string_view& rangeEnd, std::ostream& sql, NYdb::TParamsBuilder& params, size_t* paramsCounter) {
    if (key.empty())
        return {};

    const auto& keyParamName = AddParam("Key", params, key, paramsCounter);
    if (rangeEnd.empty())
        sql << keyParamName << " = `key`";
    else if (Endless == rangeEnd)
        sql << keyParamName << " <= `key`";
    else if (rangeEnd == key)
        sql << "startswith(`key`, " << keyParamName << ')';
    else
        sql << "`key` between " << keyParamName << " and " << AddParam("RangeEnd", params, rangeEnd, paramsCounter);
    return keyParamName;
}

NActors::IActor* MakeRange(std::unique_ptr<NKikimr::NGRpcService::IRequestCtx> p, TSharedStuff::TPtr stuff) {
    return new TRangeRequest(std::move(p), std::move(stuff));
}

NActors::IActor* MakePut(std::unique_ptr<NKikimr::NGRpcService::IRequestCtx> p, TSharedStuff::TPtr stuff) {
    return new TPutRequest(std::move(p), std::move(stuff));
}

NActors::IActor* MakeDeleteRange(std::unique_ptr<NKikimr::NGRpcService::IRequestCtx> p, TSharedStuff::TPtr stuff) {
    return new TDeleteRangeRequest(std::move(p), std::move(stuff));
}

NActors::IActor* MakeTxn(std::unique_ptr<NKikimr::NGRpcService::IRequestCtx> p, TSharedStuff::TPtr stuff) {
    return new TTxnRequest(std::move(p), std::move(stuff));
}

NActors::IActor* MakeCompact(std::unique_ptr<NKikimr::NGRpcService::IRequestCtx> p, TSharedStuff::TPtr stuff) {
    return new TCompactRequest(std::move(p), std::move(stuff));
}

NActors::IActor* MakeLeaseGrant(std::unique_ptr<NKikimr::NGRpcService::IRequestCtx> p, TSharedStuff::TPtr stuff) {
    return new TLeaseGrantRequest(std::move(p), std::move(stuff));
}

NActors::IActor* MakeLeaseRevoke(std::unique_ptr<NKikimr::NGRpcService::IRequestCtx> p, TSharedStuff::TPtr stuff) {
    return new TLeaseRevokeRequest(std::move(p), std::move(stuff));
}

NActors::IActor* MakeLeaseTimeToLive(std::unique_ptr<NKikimr::NGRpcService::IRequestCtx> p, TSharedStuff::TPtr stuff) {
    return new TLeaseTimeToLiveRequest(std::move(p), std::move(stuff));
}

NActors::IActor* MakeLeaseLeases(std::unique_ptr<NKikimr::NGRpcService::IRequestCtx> p, TSharedStuff::TPtr stuff) {
    return new TLeaseLeasesRequest(std::move(p), std::move(stuff));
}

} // namespace NEtcd
