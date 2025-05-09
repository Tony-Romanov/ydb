#include "etcd_oper.h"
#include "etcd_impl.h"

namespace NEtcd {

using namespace NYdb::NQuery;

namespace {

std::string GetNameWithIndex(const std::string_view& name, const size_t* counter) {
    auto param = std::string(1U, '$') += name;
    if (counter)
        param += std::to_string(*counter);
    return param;
}

void MakeSlice(const std::string_view& where, std::ostream& sql, NYdb::TParamsBuilder& params, size_t* paramsCounter, const i64 revision) {
    if (revision) {
        sql << "select * from (select max_by(TableRow(), `modified`) from `history`" << where;
        sql << " and " << AddParam("Rev", params, revision, paramsCounter) << " >= `modified`";
        sql << " group by `key`) flatten columns where 0L < `version`";
    } else {
        sql << "select * from `current`" << where;
    }
}

}

std::ostream& TRange::Dump(std::ostream& out) const {
    out << (RangeEnd.empty() ? (CountOnly ? "Has" : "Get") : (CountOnly ? "Count" : "Range")) << '(';
    DumpKeyRange(out, Key, RangeEnd);
    if (KeyRevision)
        out << ",revision=" << KeyRevision;
    if (MinCreateRevision)
        out << ",min_create_rev=" << MinCreateRevision;
    if (MaxCreateRevision)
        out << ",max_create_rev=" << MaxCreateRevision;
    if (MinModificateRevision)
        out << ",min_mod_rev=" << MinModificateRevision;
    if (MaxModificateRevision)
        out << ",max_mod_rev=" << MaxModificateRevision;
    if (const auto sort = SortOrder)
        out << ",by " << Fields[SortTarget]  << ' ' << (*sort ? "asc" : "desc");
    if (KeysOnly)
        out << ",keys";
    if (Serializable)
        out << ",serializable";
    if (Limit)
        out << ",limit=" << Limit;
    out << ')';
    return out;
}

std::string TRange::Parse(const etcdserverpb::RangeRequest& rec) {
    Key = rec.key();
    RangeEnd = DecrementKey(rec.range_end());
    KeysOnly = rec.keys_only();
    CountOnly = rec.count_only();
    Limit = rec.limit();
    KeyRevision = rec.revision();
    MinCreateRevision = rec.min_create_revision();
    MaxCreateRevision = rec.max_create_revision();
    MinModificateRevision = rec.min_mod_revision();
    MaxModificateRevision = rec.max_mod_revision();
    SortTarget = rec.sort_target();
    Serializable = rec.serializable();
    switch (rec.sort_order()) {
        case etcdserverpb::RangeRequest_SortOrder_ASCEND: SortOrder = true; break;
        case etcdserverpb::RangeRequest_SortOrder_DESCEND: SortOrder = false; break;
        default: break;
    }

    if (Key.empty())
        return "key is not provided";

    if (!RangeEnd.empty() && Endless != RangeEnd && RangeEnd < Key)
        return "invalid range end";

    return {};
}

void TRange::MakeSimpleQueryWithParams(std::ostream& sql, const std::string_view& keyFilter, NYdb::TParamsBuilder& params, size_t* paramsCounter, size_t* resultsCounter, const std::string_view& txnFilter) {
    if (resultsCounter)
        ResultIndex = (*resultsCounter)++;

    const auto& resultName = GetNameWithIndex("Output", resultsCounter);
    sql << resultName << " = select `key`,`created`,`modified`,`version`,`lease`,";
    if (KeysOnly)
        sql << "'' as ";
    sql << "`value` from (" << std::endl << '\t';
    MakeSlice(keyFilter, sql, params, paramsCounter, KeyRevision);
    sql << std::endl << ") where " << (txnFilter.empty() ? "true" : txnFilter);

    if (MinCreateRevision) {
        sql << std::endl << '\t' << "and `created` >= " << AddParam("MinCreateRevision", params, MinCreateRevision, paramsCounter);
    }

    if (MaxCreateRevision) {
        sql << std::endl << '\t' << "and `created` <= " << AddParam("MaxCreateRevision", params, MaxCreateRevision, paramsCounter);
    }

    if (MinModificateRevision) {
        sql << std::endl << '\t' << "and `modified` >= " << AddParam("MinModificateRevision", params, MinModificateRevision, paramsCounter);
    }

    if (MaxModificateRevision) {
        sql << std::endl << '\t' << "and `modified` <= " << AddParam("MaxModificateRevision", params, MaxModificateRevision, paramsCounter);
    }

    sql << ';' << std::endl;
    sql << "select count(*) from " << resultName << ';' << std::endl;

    if (!CountOnly) {
        if (resultsCounter)
            ++(*resultsCounter);

        sql << "select * from " << resultName;

        if (SortOrder) {
            sql << std::endl << "order by `" << Fields[SortTarget] << "` " << (*SortOrder ? "asc" : "desc");
        }

        if (Limit) {
            sql << std::endl << "limit " << AddParam<ui64>("Limit", params, Limit, paramsCounter);
        }
        sql << ';' << std::endl;
    }
}

void TRange::MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder& params, size_t* paramsCounter, size_t* resultsCounter, const std::string_view& txnFilter) {
    std::ostringstream where;
    where << " where ";
    if (!txnFilter.empty())
        where << txnFilter << " and ";
    MakeSimplePredicate(Key, RangeEnd, where, params, paramsCounter);
    MakeSimpleQueryWithParams(sql, where.view(), params, paramsCounter, resultsCounter);
}

etcdserverpb::RangeResponse TRange::MakeResponse(i64 revision, const NYdb::TResultSets& results) const {
    etcdserverpb::RangeResponse response;
    response.mutable_header()->set_revision(revision);

    ui64 count = 0ULL;
    if (auto parser = NYdb::TResultSetParser(results[ResultIndex]); parser.TryNextRow()) {
        count = NYdb::TValueParser(parser.GetValue(0)).GetUint64();
        response.set_count(count);
    }

    if (!CountOnly) {
        const auto& output = results[ResultIndex + 1U];
        if (output.RowsCount() < count)
            response.set_more(true);

        for (auto parser = NYdb::TResultSetParser(output); parser.TryNextRow();) {
            const auto kvs = response.add_kvs();
            kvs->set_key(NYdb::TValueParser(parser.GetValue("key")).GetString());
            kvs->set_value(NYdb::TValueParser(parser.GetValue("value")).GetString());
            kvs->set_mod_revision(NYdb::TValueParser(parser.GetValue("modified")).GetInt64());
            kvs->set_create_revision(NYdb::TValueParser(parser.GetValue("created")).GetInt64());
            kvs->set_version(NYdb::TValueParser(parser.GetValue("version")).GetInt64());
            kvs->set_lease(NYdb::TValueParser(parser.GetValue("lease")).GetInt64());
        }
    }
    return response;
}

std::ostream& TPut::Dump(std::ostream& out) const {
    out << "Put(" << Key;
    if (IgnoreValue)
        out << ",ignore value";
    else
        out << ",size=" << Value.size();
    if (IgnoreLease)
        out << ",ignore lease";
    else if (Lease)
        out << ",lease=" << Lease;
    if (GetPrevious)
        out << ",previous";
    out << ')';
    return out;
}

std::string TPut::Parse(const etcdserverpb::PutRequest& rec) {
    Key = rec.key();
    Value = rec.value();
    Lease = rec.lease();
    GetPrevious = rec.prev_kv();
    IgnoreValue = rec.ignore_value();
    IgnoreLease = rec.ignore_lease();

    if (Key.empty())
        return "key is not provided";

    if (IgnoreValue && !Value.empty())
        return "value is provided";

    if (IgnoreLease && Lease)
        return "lease is provided";

    return {};
}

void TPut::MakeSimpleQueryWithParams(std::ostream& sql, const std::string_view& keyParamName, const std::string_view& keyFilter, NYdb::TParamsBuilder& params, size_t* paramsCounter, size_t* resultsCounter, const std::string_view& txnFilter) {
    const auto& valueParamName = IgnoreValue ? std::string("NULL") : AddParam("Value", params, Value, paramsCounter);
    const auto& leaseParamName = IgnoreLease ? std::string("NULL") : AddParam("Lease", params, Lease, paramsCounter);

    const auto& oldResultSetName = GetNameWithIndex("Old", resultsCounter);
    const auto& newResultSetName = GetNameWithIndex("New", resultsCounter);

    sql << oldResultSetName << " = select * from `current`" << keyFilter << ';' << std::endl;
    sql << newResultSetName << " = select" << std::endl;
    sql << '\t' << keyParamName << " as `key`," << std::endl;
    sql << '\t' << "if(`version` > 0L, `created`, $Revision) as `created`," << std::endl;
    sql << '\t' << "$Revision as `modified`," << std::endl;
    sql << '\t' << "`version` + 1L as `version`," << std::endl;
    sql << '\t' << "nvl(" << valueParamName << ",`value`) as `value`," << std::endl;
    sql << '\t' << "nvl(" << leaseParamName << ",`lease`) as `lease`" << std::endl;
    sql << '\t' << "from ";
    if (!txnFilter.empty())
        sql << "(select * from ";

    const bool update = IgnoreValue || IgnoreLease;
    if (update)
        sql << oldResultSetName;
    else
        sql << "(select * from " << oldResultSetName <<" union all select * from as_table([<|`key`:" << keyParamName << ", `created`:0L, `modified`: 0L, `version`:0L, `value`:'', `lease`:0L|>]) order by `created` desc limit 1)";

    if (!txnFilter.empty())
        sql << " where " << txnFilter << ')';
    sql << ';' << std::endl;

    sql << (update ? "update `current` on" : "upsert into `current`") << " select * from " << newResultSetName << ';' << std::endl;
    sql << "insert into `history` select * from " << newResultSetName << ';' << std::endl;

    if (GetPrevious || NotifyWatchtower || update) {
        if (resultsCounter)
            ResultIndex = (*resultsCounter)++;
        sql << "select `value`, `created`, `modified`, `version`, `lease` from " << oldResultSetName << " where `version` > 0L;" << std::endl;
    }
    if constexpr (NotifyWatchtower) {
        if (resultsCounter)
            ++(*resultsCounter);
        sql << "select `value`, `created`, `modified`, `version`, `lease` from " << newResultSetName << ';' << std::endl;
    }
}

void TPut::MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder& params, size_t* paramsCounter, size_t* resultsCounter, const std::string_view& txnFilter) {
    std::ostringstream keyFilter;
    keyFilter << " where ";
    const auto& keyParamName = MakeSimplePredicate(Key, {}, keyFilter, params, paramsCounter);
    MakeSimpleQueryWithParams(sql, keyParamName, keyFilter.view(), params, paramsCounter, resultsCounter, txnFilter);
}

std::variant<etcdserverpb::PutResponse, TGrpcError>
TPut::MakeResponse(i64 revision, const NYdb::TResultSets& results, const TNotifier& notifier) const {
    etcdserverpb::PutResponse response;
    response.mutable_header()->set_revision(revision);

    if (GetPrevious || IgnoreValue || IgnoreValue) {
        if (auto parser = NYdb::TResultSetParser(results[ResultIndex]); parser.TryNextRow() && 5ULL == parser.ColumnsCount()) {
            if (GetPrevious) {
                const auto prev = response.mutable_prev_kv();
                prev->set_key(Key);
                prev->set_value(NYdb::TValueParser(parser.GetValue("value")).GetString());
                prev->set_mod_revision(NYdb::TValueParser(parser.GetValue("modified")).GetInt64());
                prev->set_create_revision(NYdb::TValueParser(parser.GetValue("created")).GetInt64());
                prev->set_version(NYdb::TValueParser(parser.GetValue("version")).GetInt64());
                prev->set_lease(NYdb::TValueParser(parser.GetValue("lease")).GetInt64());
            }
        } else if (IgnoreValue || IgnoreValue) {
            return std::make_pair(grpc::StatusCode::NOT_FOUND, std::string("key not found"));
        }
    }
    if (NotifyWatchtower && notifier) {
        TData oldData, newData;
        if (auto parser = NYdb::TResultSetParser(results[ResultIndex]); parser.TryNextRow() && 5ULL == parser.ColumnsCount()) {
            oldData.Value = NYdb::TValueParser(parser.GetValue("value")).GetString();
            oldData.Created = NYdb::TValueParser(parser.GetValue("created")).GetInt64();
            oldData.Modified = NYdb::TValueParser(parser.GetValue("modified")).GetInt64();
            oldData.Version = NYdb::TValueParser(parser.GetValue("version")).GetInt64();
            oldData.Lease = NYdb::TValueParser(parser.GetValue("lease")).GetInt64();
        }
        if (auto parser = NYdb::TResultSetParser(results[ResultIndex + 1U]); parser.TryNextRow() && 5ULL == parser.ColumnsCount()) {
            newData.Value = NYdb::TValueParser(parser.GetValue("value")).GetString();
            newData.Created = NYdb::TValueParser(parser.GetValue("created")).GetInt64();
            newData.Modified = NYdb::TValueParser(parser.GetValue("modified")).GetInt64();
            newData.Version = NYdb::TValueParser(parser.GetValue("version")).GetInt64();
            newData.Lease = NYdb::TValueParser(parser.GetValue("lease")).GetInt64();
            notifier(std::string(Key), revision, std::move(oldData), std::move(newData));
        }
    }
    return response;
}

std::ostream& TDeleteRange::Dump(std::ostream& out) const {
    out << "Delete(";
    DumpKeyRange(out, Key, RangeEnd);
    if (GetPrevious)
        out << ",previous";
    out << ')';
    return out;
}

std::string TDeleteRange::Parse(const etcdserverpb::DeleteRangeRequest& rec) {
    Key = rec.key();
    RangeEnd = DecrementKey(rec.range_end());
    GetPrevious = rec.prev_kv();

    if (Key.empty())
        return "key is not provided";

    if (!RangeEnd.empty() && Endless != RangeEnd && RangeEnd < Key)
        return "invalid range end";

    return {};
}

void TDeleteRange::MakeSimpleQueryWithParams(std::ostream& sql, const std::string_view& keyFilter, size_t* resultsCounter, const std::string_view& txnFilter) {
    if (resultsCounter)
        ResultIndex = (*resultsCounter)++;

    const auto& oldResultSetName = GetNameWithIndex("Old", resultsCounter);
    sql << oldResultSetName << " = select * from `current`" << keyFilter;
    if (!txnFilter.empty())
        sql << " and " << txnFilter;
    sql << ';' << std::endl;

    sql << "insert into `history`" << std::endl;
    sql << "select `key`, `created`, $Revision as `modified`, 0L as `version`, `value`, `lease` from " << oldResultSetName << ';' << std::endl;

    sql << "select count(*) from " << oldResultSetName << ';' << std::endl;
    if (GetPrevious || NotifyWatchtower) {
        if (resultsCounter)
            ++(*resultsCounter);
        sql << "select `key`, `value`, `created`, `modified`, `version`, `lease` from " << oldResultSetName << ';' << std::endl;
    }
    sql << "delete from `current`" << keyFilter;
    if (!txnFilter.empty())
        sql << " and " << txnFilter;
    sql << ';' << std::endl;
}

void TDeleteRange::MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder& params, size_t* paramsCounter, size_t* resultsCounter, const std::string_view& txnFilter) {
    std::ostringstream keyFilter;
    keyFilter << " where ";
    MakeSimplePredicate(Key, RangeEnd, keyFilter, params, paramsCounter);
    MakeSimpleQueryWithParams(sql, keyFilter.view(), resultsCounter, txnFilter);
}

etcdserverpb::DeleteRangeResponse TDeleteRange::MakeResponse(i64 revision, const NYdb::TResultSets& results, const TNotifier& notifier) const {
    etcdserverpb::DeleteRangeResponse response;

    ui64 deleted  = 0ULL;
    if (auto parser = NYdb::TResultSetParser(results[ResultIndex]); parser.TryNextRow()) {
        deleted = NYdb::TValueParser(parser.GetValue(0)).GetUint64();
        response.set_deleted(deleted);
    }

    response.mutable_header()->set_revision(deleted ? revision : revision - 1LL);

    if (GetPrevious) {
        for (auto parser = NYdb::TResultSetParser(results[ResultIndex + 1U]); parser.TryNextRow();) {
            const auto kvs = response.add_prev_kvs();
            kvs->set_key(NYdb::TValueParser(parser.GetValue("key")).GetString());
            kvs->set_value(NYdb::TValueParser(parser.GetValue("value")).GetString());
            kvs->set_mod_revision(NYdb::TValueParser(parser.GetValue("modified")).GetInt64());
            kvs->set_create_revision(NYdb::TValueParser(parser.GetValue("created")).GetInt64());
            kvs->set_version(NYdb::TValueParser(parser.GetValue("version")).GetInt64());
            kvs->set_lease(NYdb::TValueParser(parser.GetValue("lease")).GetInt64());
        }
    }

    if (NotifyWatchtower && notifier) {
        for (auto parser = NYdb::TResultSetParser(results[ResultIndex + 1U]); parser.TryNextRow();) {
            TData oldData {
                .Value = NYdb::TValueParser(parser.GetValue("value")).GetString(),
                .Created = NYdb::TValueParser(parser.GetValue("created")).GetInt64(),
                .Modified = NYdb::TValueParser(parser.GetValue("modified")).GetInt64(),
                .Version = NYdb::TValueParser(parser.GetValue("version")).GetInt64(),
                .Lease = NYdb::TValueParser(parser.GetValue("lease")).GetInt64()
            };
            auto key = NYdb::TValueParser(parser.GetValue("key")).GetString();
            notifier(std::move(key), revision, std::move(oldData), {});
        }
    }
    return response;
}

std::string TCompare::Parse(const etcdserverpb::Compare& rec) {
    Key = rec.key();
    RangeEnd = DecrementKey(rec.range_end());
    Result = rec.result();
    Target = rec.target();
    switch (rec.target()) {
        case etcdserverpb::Compare_CompareTarget_VERSION:
            Value = rec.version();
            break;
        case etcdserverpb::Compare_CompareTarget_CREATE:
            Value = rec.create_revision();
            break;
        case etcdserverpb::Compare_CompareTarget_MOD:
            Value = rec.mod_revision();
            break;
        case etcdserverpb::Compare_CompareTarget_VALUE:
            Value = rec.value();
            break;
        case etcdserverpb::Compare_CompareTarget_LEASE:
            Value = rec.lease();
            break;
        default:
            break;
    }

    if (Key.empty())
        return "key is not provided";

    if (!RangeEnd.empty() && Endless != RangeEnd && RangeEnd < Key)
        return "invalid range end";

    return {};
}

std::ostream& TCompare::Dump(std::ostream& out) const {
    out << Fields[Target] << '(';
    DumpKeyRange(out, Key, RangeEnd);
    out << ')' << Comparator[Result];
    if (const auto val = std::get_if<std::string>(&Value))
        out << *val;
    else if (const auto val = std::get_if<i64>(&Value))
        out << *val;
    out << ')';
    return out;
}

// return default value if key is absent.
bool TCompare::MakeQueryWithParams(std::ostream& positive, std::ostream& negative, NYdb::TParamsBuilder& params, size_t* paramsCounter) const {
    positive << '`' << Fields[Target] << '`' << ' ' << Comparator[Result] << ' ';
    negative << '`' << Fields[Target] << '`' << ' ' << Inverted[Result] << ' ';
    if (const auto val = std::get_if<std::string>(&Value)) {
        const auto& valueParamName = AddParam("Value", params, *val, paramsCounter);
        positive << valueParamName;
        negative << valueParamName;
    } else if (const auto val = std::get_if<i64>(&Value)) {
        const auto& argParamName = AddParam("Arg", params, *val, paramsCounter);
        positive << argParamName;
        negative << argParamName;
        return !*val && Target < 3U;
    }
    return false;
}

std::ostream& TTxn::Dump(std::ostream& out) const {
    const auto dump = [](const std::vector<TRequestOp>& operations, std::ostream& out) {
        for (const auto& operation : operations) {
            if (const auto oper = std::get_if<TRange>(&operation))
                oper->Dump(out);
            else if (const auto oper = std::get_if<TPut>(&operation))
                oper->Dump(out);
            else if (const auto oper = std::get_if<TDeleteRange>(&operation))
                oper->Dump(out);
            else if (const auto oper = std::get_if<TTxn>(&operation))
                oper->Dump(out);
        }
    };

    out << "Txn(";
    if (!Compares.empty()) {
        out << "if ";
        for (const auto& cmp : Compares)
            cmp.Dump(out);
    }
    if (!Success.empty()) {
        out << " then ";
        dump(Success, out);
    }
    if (!Failure.empty()) {
        out << " else ";
        dump(Failure, out);
    }
    out << ')';
    return out;
}

bool TTxn::IsReadOnly() const {
    for (const auto& operation : Success) {
        if (const auto oper = std::get_if<TTxn>(&operation)) {
            if (!oper->IsReadOnly())
                return false;
        } else if (!std::get_if<TRange>(&operation))
            return false;
    }

    for (const auto& operation : Failure) {
        if (const auto oper = std::get_if<TTxn>(&operation)) {
            if (!oper->IsReadOnly())
                return false;
        } else if (!std::get_if<TRange>(&operation))
            return false;
    }

    return true;
}

TKeysSet TTxn::GetKeys() const {
    TKeysSet keys;
    for (const auto& compare : Compares)
        keys.emplace(compare.Key, compare.RangeEnd);

    const auto get = [](const std::vector<TRequestOp>& operations, TKeysSet& keys) {
        for (const auto& operation : operations) {
            if (const auto oper = std::get_if<TRange>(&operation))
                keys.emplace(oper->Key, oper->RangeEnd);
            else if (const auto oper = std::get_if<TPut>(&operation))
                keys.emplace(oper->Key, std::string());
            else if (const auto oper = std::get_if<TDeleteRange>(&operation))
                keys.emplace(oper->Key, oper->RangeEnd);
            else if (const auto oper = std::get_if<TTxn>(&operation))
                keys.merge(oper->GetKeys());
        }
    };
    get(Success, keys);
    get(Failure, keys);
    return keys;
}

template<class TOperation, class TSrc>
std::string TTxn::Parse(std::vector<TRequestOp>& operations, const TSrc& src) {
    TOperation op;
    if (const auto& error = op.Parse(src); !error.empty())
        return error;
    operations.emplace_back(std::move(op));
    return {};
}

std::string TTxn::Parse(const etcdserverpb::TxnRequest& rec) {
    for (const auto& comp : rec.compare()) {
        Compares.emplace_back();
        if (const auto& error = Compares.back().Parse(comp); !error.empty())
            return error;
    }

    const auto fill = [](std::vector<TRequestOp>& operations, const auto& fields) -> std::string {
        for (const auto& op : fields) {
            switch (op.request_case()) {
                case etcdserverpb::RequestOp::RequestCase::kRequestRange: {
                    if (const auto& error = Parse<TRange>(operations, op.request_range()); !error.empty())
                        return error;
                    break;
                }
                case etcdserverpb::RequestOp::RequestCase::kRequestPut: {
                    if (const auto& error = Parse<TPut>(operations, op.request_put()); !error.empty())
                        return error;
                    break;
                }
                case etcdserverpb::RequestOp::RequestCase::kRequestDeleteRange: {
                    if (const auto& error = Parse<TDeleteRange>(operations, op.request_delete_range()); !error.empty())
                        return error;
                    break;
                }
                case etcdserverpb::RequestOp::RequestCase::kRequestTxn: {
                    if (const auto& error = Parse<TTxn>(operations, op.request_txn()); !error.empty())
                        return error;
                    break;
                }
                default:
                    return "invalid operation";
            }
        }
        return {};
    };

    return fill(Success, rec.success()) + fill(Failure, rec.failure());
}

void TTxn::MakeSimpleQueryWithParams(std::ostream& sql, const std::string_view& keyParamName, bool singleKey, const std::string_view& keyFilter, NYdb::TParamsBuilder& params, size_t* paramsCounter, size_t* resultsCounter, const std::string_view& txnFilter) {
    ResultIndex = (*resultsCounter)++;

    const auto make = [&sql, &params](std::vector<TRequestOp>& operations, size_t* paramsCounter, size_t* resultsCounter, const std::string_view& keyFilter, const std::string_view& keyParamName, bool singleKey, const std::string_view& txnFilter) {
        for (auto& operation : operations) {
            if (const auto oper = std::get_if<TRange>(&operation))
                oper->MakeSimpleQueryWithParams(sql, keyFilter, params, paramsCounter, resultsCounter, txnFilter);
            else if (const auto oper = std::get_if<TPut>(&operation))
                oper->MakeSimpleQueryWithParams(sql, keyParamName, keyFilter, params, paramsCounter, resultsCounter, txnFilter);
            else if (const auto oper = std::get_if<TDeleteRange>(&operation))
                oper->MakeSimpleQueryWithParams(sql, keyFilter, resultsCounter, txnFilter);
            else if (const auto oper = std::get_if<TTxn>(&operation))
                oper->MakeSimpleQueryWithParams(sql, keyParamName, singleKey, keyFilter, params, paramsCounter, resultsCounter, txnFilter);
        }
    };

    std::ostringstream thenFilter, elseFilter;
    bool def = true;
    for (auto j = Compares.cbegin(); Compares.cend() != j; ++j) {
        if (Compares.cbegin() != j) {
            thenFilter << " and ";
            elseFilter << " or ";
        }
        def = j->MakeQueryWithParams(thenFilter, elseFilter, params, paramsCounter) && def;
    }

    if (Compares.empty()) {
        sql << "select true;" << std::endl;
        make(Success, paramsCounter, resultsCounter, keyFilter, keyParamName, singleKey, txnFilter);
    } else {
        std::ostringstream thenExtra, elseExtra;
        if (!txnFilter.empty()) {
            thenExtra << txnFilter << " and ";
            elseExtra << txnFilter << " and ";
        }
        thenExtra << '(' << thenFilter.view() << ')';
        elseExtra << '(' << elseFilter.view() << ')';

        if (!singleKey)
                sql << "select bool_and(`cmp`) as`cmp` from (";
        sql << "select nvl(" << (def ? '0' : '1') << "UL = count(*), " << (def ? "true" : "false") << ") as `cmp` from `current`" << keyFilter << " and " << (def ? elseFilter : thenFilter).view();
        if (!singleKey)
            sql << " group by `key`)";
        sql << ';' << std::endl;

        make(Success, paramsCounter, resultsCounter, keyFilter, keyParamName, singleKey, thenExtra.view());
        make(Failure, paramsCounter, resultsCounter, keyFilter, keyParamName, singleKey, elseExtra.view());
    }
}

void TTxn::MakeQueryWithParams(std::ostream& sql, NYdb::TParamsBuilder& params, size_t* paramsCounter, size_t* resultsCounter, const std::string_view& txnFilter) {
    if (const auto& keys = GetKeys(); keys.empty()) {
        return MakeSimpleQueryWithParams(sql, {}, true, {}, params, resultsCounter, paramsCounter);
    } else if (1U == keys.size()) {
        std::ostringstream where;
        where << " where ";
        const auto& keyParamName = MakeSimplePredicate(keys.cbegin()->first, keys.cbegin()->second, where, params);
        return MakeSimpleQueryWithParams(sql, keyParamName, keys.cbegin()->second.empty(), where.view(), params, resultsCounter, paramsCounter);
    };

    ResultIndex = (*resultsCounter)++;

    const auto make = [&sql, &params](std::vector<TRequestOp>& operations, size_t* paramsCounter, size_t* resultsCounter, const std::string_view& txnFilter) {
        for (auto& operation : operations) {
            if (const auto oper = std::get_if<TRange>(&operation))
                oper->MakeQueryWithParams(sql, params, paramsCounter, resultsCounter, txnFilter);
            else if (const auto oper = std::get_if<TPut>(&operation))
                oper->MakeQueryWithParams(sql, params, paramsCounter, resultsCounter, txnFilter);
            else if (const auto oper = std::get_if<TDeleteRange>(&operation))
                oper->MakeQueryWithParams(sql, params, paramsCounter, resultsCounter, txnFilter);
            else if (const auto oper = std::get_if<TTxn>(&operation))
                oper->MakeQueryWithParams(sql, params, paramsCounter, resultsCounter, txnFilter);
        }
    };

    if (Compares.empty()) {
        sql << "select true;" << std::endl;
        make(Success, paramsCounter, resultsCounter, "");
    } else {
        std::map<std::pair<std::string, std::string>, std::vector<TCompare>> map;
        for (const auto& compare : Compares)
            map[std::make_pair(compare.Key, compare.RangeEnd)].emplace_back(compare);
        const bool manyRanges = map.size() > 1U;

        const auto& cmpResultSetName = GetNameWithIndex("Cmp", resultsCounter);
        sql << cmpResultSetName << " = ";

        if (manyRanges)
            sql << "select nvl(bool_and(`cmp`), false) as `cmp` from (" << std::endl;

        for (auto i = map.cbegin(); map.cend() != i; ++i) {
            if (map.cbegin() != i)
                sql << std::endl << "union all" << std::endl;

            sql << "select nvl(bool_and(";
            const auto& compares = i->second;
            bool def = true;
            for (auto j = compares.cbegin(); compares.cend() != j; ++j) {
                if (compares.cbegin() != j)
                    sql << " and ";
                std::ostringstream stub;
                def = j->MakeQueryWithParams(sql, stub, params, paramsCounter) && def;
            }
            sql << "), " << (def ? "true" : "false") << ") as `cmp` from `current` where ";
            MakeSimplePredicate(i->first.first, i->first.second, sql, params, paramsCounter);
        }

        if (manyRanges)
            sql << std::endl << ')';

        sql << ';' << std::endl;
        sql << "select * from " << cmpResultSetName << ';' << std::endl;

        const auto& scalarSuccessName = GetNameWithIndex("Success", resultsCounter);
        const auto& scalarFailureName = GetNameWithIndex("Failure", resultsCounter);

        if (txnFilter.empty()) {
            if (!Success.empty())
                sql << scalarSuccessName << " = select " << cmpResultSetName << ';' << std::endl;
            if (!Failure.empty())
                sql << scalarFailureName << " = select not " << cmpResultSetName << ';' << std::endl;
        } else {
            if (!Success.empty())
                sql << scalarSuccessName << " = select " << txnFilter << " and " << cmpResultSetName << ';' << std::endl;
            if (!Failure.empty())
                sql << scalarFailureName << " = select " << txnFilter << " and not " << cmpResultSetName << ';' << std::endl;
        }

        make(Success, paramsCounter, resultsCounter, scalarSuccessName);
        make(Failure, paramsCounter, resultsCounter, scalarFailureName);
    }
}

std::variant<etcdserverpb::TxnResponse, TGrpcError>
TTxn::MakeResponse(i64 revision, const NYdb::TResultSets& results, const TNotifier& notifier) const {
    etcdserverpb::TxnResponse response;
    response.mutable_header()->set_revision(revision);

    if (auto parser = NYdb::TResultSetParser(results[ResultIndex]); parser.TryNextRow()) {
        const bool succeeded = NYdb::TValueParser(parser.GetValue(0)).GetBool();
        response.set_succeeded(succeeded);
        for (const auto& operation : succeeded ? Success : Failure) {
            const auto resp = response.add_responses();
            if (const auto oper = std::get_if<TRange>(&operation)) {
                *resp->mutable_response_range() = oper->MakeResponse(revision, results);
            } else if (const auto oper = std::get_if<TPut>(&operation)) {
                const auto& res = oper->MakeResponse(revision, results, notifier);
                if (const auto good = std::get_if<etcdserverpb::PutResponse>(&res))
                    *resp->mutable_response_put() = *good;
                else if (const auto bad = std::get_if<TGrpcError>(&res))
                    return *bad;
            } else if (const auto oper = std::get_if<TDeleteRange>(&operation)) {
                *resp->mutable_response_delete_range() = oper->MakeResponse(revision, results, notifier);
            } else if (const auto oper = std::get_if<TTxn>(&operation)) {
                const auto& res = oper->MakeResponse(revision, results, notifier);
                if (const auto good = std::get_if<etcdserverpb::TxnResponse>(&res))
                    *resp->mutable_response_txn() = *good;
                else if (const auto bad = std::get_if<TGrpcError>(&res))
                    return *bad;
            }
        }
    }
    return response;
}

} // namespace NEtcd

