#pragma once

#include "etcd_events.h"

#include <ydb/apps/etcd_proxy/proto/rpc.grpc.pb.h>

namespace NEtcd {

struct TOperation {
    size_t ResultIndex = 0ULL;
};

struct TRange : public TOperation {
    std::string Key, RangeEnd;
    bool KeysOnly, CountOnly, Serializable;
    ui64 Limit;
    i64 KeyRevision;
    i64 MinCreateRevision, MaxCreateRevision;
    i64 MinModificateRevision, MaxModificateRevision;
    std::optional<bool> SortOrder;
    size_t SortTarget;

    static constexpr std::string_view Fields[] = {"key"sv, "version"sv, "created"sv, "modified"sv, "value"sv};

    std::ostream& Dump(std::ostream& out) const;

    std::string Parse(const etcdserverpb::RangeRequest& rec);

    void MakeQueryWithParams(std::ostream& sql, const std::string_view& keyFilter, NYdb::TParamsBuilder& params, size_t* paramsCounter = nullptr, size_t* resultsCounter = nullptr, const std::string_view& txnFilter = {});

    void MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder& params, size_t* paramsCounter = nullptr, size_t* resultsCounter = nullptr, const std::string_view& txnFilter = {});
    etcdserverpb::RangeResponse MakeResponse(i64 revision, const NYdb::TResultSets& results) const;
};

using TNotifier = std::function<void(std::string&&, i64, TData&&, TData&&)>;
using TGrpcError = std::pair<grpc::StatusCode, std::string>;

struct TPut : public TOperation {
    std::string Key, Value;
    i64 Lease = 0LL;
    bool GetPrevious = false;
    bool IgnoreValue = false;
    bool IgnoreLease = false;

    std::ostream& Dump(std::ostream& out) const;

    std::string Parse(const etcdserverpb::PutRequest& rec);

    void MakeQueryWithParams(std::ostream& sql, const std::string_view& keyParamName, const std::string_view& keyFilter, NYdb::TParamsBuilder& params, size_t* paramsCounter = nullptr, size_t* resultsCounter = nullptr, const std::string_view& txnFilter = {});

    void MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder& params, size_t* paramsCounter = nullptr, size_t* resultsCounter = nullptr, const std::string_view& txnFilter = {});

    std::variant<etcdserverpb::PutResponse, TGrpcError>
    MakeResponse(i64 revision, const NYdb::TResultSets& results, const TNotifier& notifier) const;
};

struct TDeleteRange : public TOperation {
    std::string Key, RangeEnd;
    bool GetPrevious = false;

    std::ostream& Dump(std::ostream& out) const;

    std::string Parse(const etcdserverpb::DeleteRangeRequest& rec);

    void MakeQueryWithParams(std::ostream& sql, const std::string_view& keyFilter, size_t* resultsCounter = nullptr, const std::string_view& txnFilter = {});

    void MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder& params, size_t* paramsCounter = nullptr, size_t* resultsCounter = nullptr, const std::string_view& txnFilter = {});

    etcdserverpb::DeleteRangeResponse MakeResponse(i64 revision, const NYdb::TResultSets& results, const TNotifier& notifier) const;
};

struct TCompare {
    std::string Key, RangeEnd;

    std::variant<i64, std::string> Value;

    size_t Result, Target;

    std::string Parse(const etcdserverpb::Compare& rec);

    static constexpr std::string_view Fields[] = {"version"sv, "created"sv, "modified"sv, "value"sv, "lease"sv};
    static constexpr std::string_view Comparator[] = {"="sv, ">"sv, "<"sv, "!="sv};
    static constexpr std::string_view Inverted[] = {"!="sv, "<="sv, ">="sv, "="sv};

    std::ostream& Dump(std::ostream& out) const;

    // return default value if key is absent.
    bool MakeQueryWithParams(std::ostream& positive, std::ostream& negative, NYdb::TParamsBuilder& params, size_t* paramsCounter) const;
};

struct TTxn : public TOperation {
    using TRequestOp = std::variant<TRange, TPut, TDeleteRange, TTxn>;

    std::vector<TCompare> Compares;
    std::vector<TRequestOp> Success, Failure;

    std::ostream& Dump(std::ostream& out) const;

    bool IsReadOnly() const;

    void GetKeys(TKeysSet& keys) const;

    template<class TOperation, class TSrc>
    static std::string Parse(std::vector<TRequestOp>& operations, const TSrc& src);

    std::string Parse(const etcdserverpb::TxnRequest& rec);

    void MakeQueryWithParams(std::ostream& sql, const std::string_view& keyParamName, bool singleKey, const std::string_view& keyFilter, NYdb::TParamsBuilder& params, size_t* paramsCounter = nullptr, size_t* resultsCounter = nullptr, const std::string_view& txnFilter = {});

    void MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder& params, size_t* paramsCounter = nullptr, size_t* resultsCounter = nullptr, const std::string_view& txnFilter = {});

    std::variant<etcdserverpb::TxnResponse, TGrpcError>
    MakeResponse(i64 revision, const NYdb::TResultSets& results, const TNotifier& notifier) const;
};

}

