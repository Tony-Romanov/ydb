#include "node.h"
#include "context.h"

#include <yql/essentials/utils/yql_panic.h>

using namespace NYql;

namespace NSQLTranslationV0 {

static const TMap<ESQLWriteColumnMode, EWriteColumnMode> sqlIntoMode2WriteColumn = {
    {ESQLWriteColumnMode::InsertInto, EWriteColumnMode::Insert},
    {ESQLWriteColumnMode::InsertOrAbortInto, EWriteColumnMode::InsertOrAbort},
    {ESQLWriteColumnMode::InsertOrIgnoreInto, EWriteColumnMode::InsertOrIgnore},
    {ESQLWriteColumnMode::InsertOrRevertInto, EWriteColumnMode::InsertOrRevert},
    {ESQLWriteColumnMode::UpsertInto, EWriteColumnMode::Upsert},
    {ESQLWriteColumnMode::ReplaceInto, EWriteColumnMode::Replace},
    {ESQLWriteColumnMode::InsertIntoWithTruncate, EWriteColumnMode::Renew},
    {ESQLWriteColumnMode::Update, EWriteColumnMode::Update},
    {ESQLWriteColumnMode::Delete, EWriteColumnMode::Delete},
};

class TModifySourceBase: public ISource {
public:
    TModifySourceBase(TPosition pos, const TVector<TString>& columnsHint)
        : ISource(pos)
        , ColumnsHint(columnsHint)
    {
    }

    bool AddFilter(TContext& ctx, TNodePtr filter) override {
        Y_UNUSED(filter);
        ctx.Error(Pos) << "Source does not allow filtering";
        return false;
    }

    bool AddGroupKey(TContext& ctx, const TString& column) override {
        Y_UNUSED(column);
        ctx.Error(Pos) << "Source does not allow grouping";
        return false;
    }

    bool AddAggregation(TContext& ctx, TAggregationPtr aggr) override {
        Y_UNUSED(aggr);
        ctx.Error(Pos) << "Source does not allow aggregation";
        return false;
    }

    TNodePtr BuildFilter(TContext& ctx, const TString& label, const TNodePtr& groundNode) override {
        Y_UNUSED(ctx);
        Y_UNUSED(label);
        Y_UNUSED(groundNode);
        return nullptr;
    }

    TNodePtr BuildAggregation(const TString& label) override {
        Y_UNUSED(label);
        return nullptr;
    }

protected:
    TVector<TString> ColumnsHint;
    TString OperationHumanName;
};

class TUpdateByValues: public TModifySourceBase {
public:
    TUpdateByValues(TPosition pos, const TString& operationHumanName, const TVector<TString>& columnsHint, const TVector<TNodePtr>& values)
        : TModifySourceBase(pos, columnsHint)
        , OperationHumanName(operationHumanName)
        , Values(values)
    {}

    bool DoInit(TContext& ctx, ISource* src) override {
        if (ColumnsHint.size() != Values.size()) {
            ctx.Error(Pos) << "VALUES have " << Values.size() << " columns, " << OperationHumanName << " expects: " << ColumnsHint.size();
            return false;
        }
        for (auto& value: Values) {
            if (!value->Init(ctx, src)) {
                return false;
            }
        }
        return true;
    }

    TNodePtr Build(TContext& ctx) override {
        Y_UNUSED(ctx);
        YQL_ENSURE(Values.size() == ColumnsHint.size());

        auto structObj = Y("AsStruct");
        for (size_t i = 0; i < Values.size(); ++i) {
            TString column = ColumnsHint[i];
            TNodePtr value = Values[i];

            structObj = L(structObj, Q(Y(Q(column), value)));
        }

        auto updateRow = BuildLambda(Pos, Y("row"), structObj);
        return updateRow;
    }

    TNodePtr DoClone() const final {
        return new TUpdateByValues(Pos, OperationHumanName, ColumnsHint, CloneContainer(Values));
    }
private:
    TString OperationHumanName;

protected:
    TVector<TNodePtr> Values;
};

class TModifyByValues: public TModifySourceBase {
public:
    TModifyByValues(TPosition pos, const TString& operationHumanName, const TVector<TString>& columnsHint, const TVector<TVector<TNodePtr>>& values)
        : TModifySourceBase(pos, columnsHint)
        , OperationHumanName(operationHumanName)
        , Values(values)
    {
        FakeSource = BuildFakeSource(pos);
    }

    bool DoInit(TContext& ctx, ISource* src) override {
        Y_UNUSED(src);
        bool hasError = false;
        for (const auto& row: Values) {
            if (ColumnsHint.empty()) {
                ctx.Error(Pos) << OperationHumanName << " ... VALUES requires specification of table columns";
                hasError = true;
                continue;
            }
            if (ColumnsHint.size() != row.size()) {
                ctx.Error(Pos) << "VALUES have " << row.size() << " columns, " << OperationHumanName << " expects: " << ColumnsHint.size();
                hasError = true;
                continue;
            }
            for (auto& value: row) {
                if (!value->Init(ctx, FakeSource.Get())) {
                    hasError = true;
                    continue;
                }
            }
        }
        return !hasError;
    }

    TNodePtr Build(TContext& ctx) override {
        Y_UNUSED(ctx);
        auto tuple = Y();
        for (const auto& row: Values) {
            auto rowValues = Y("AsStruct");
            auto column = ColumnsHint.begin();
            for (auto value: row) {
                rowValues = L(rowValues, Q(Y(BuildQuotedAtom(Pos, *column), value)));
                ++column;
            }
            tuple = L(tuple, rowValues);
        }
        return Y("EnsurePersistable", Q(tuple));
    }

    TNodePtr DoClone() const final {
        TVector<TVector<TNodePtr>> clonedValues;
        clonedValues.reserve(Values.size());
        for (auto cur: Values) {
            clonedValues.push_back(CloneContainer(cur));
        }
        return new TModifyByValues(Pos, OperationHumanName, ColumnsHint, clonedValues);
    }

private:
    TString OperationHumanName;
    TVector<TVector<TNodePtr>> Values;
    TSourcePtr FakeSource;
};

class TModifyBySource: public TModifySourceBase {
public:
    TModifyBySource(TPosition pos, const TString& operationHumanName, const TVector<TString>& columnsHint, TSourcePtr source)
        : TModifySourceBase(pos, columnsHint)
        , OperationHumanName(operationHumanName)
        , Source(std::move(source))
    {}

    void GetInputTables(TTableList& tableList) const override {
        if (Source) {
            return Source->GetInputTables(tableList);
        }
    }

    bool DoInit(TContext& ctx, ISource* src) override {
        if (!Source->Init(ctx, src)) {
            return false;
        }
        const auto& sourceColumns = Source->GetColumns();
        const auto numColumns = !ColumnsHint.empty() && sourceColumns ? sourceColumns->List.size() : 0;
        if (ColumnsHint.size() != numColumns) {
            ctx.Error(Pos) << "SELECT has " << numColumns << " columns, " << OperationHumanName << " expects: " << ColumnsHint.size();
            return false;
        }
        if (numColumns) {
            TStringStream str;
            bool mismatchFound = false;
            for (size_t i = 0; i < numColumns; ++i) {
                bool hasName = sourceColumns->NamedColumns[i];
                if (hasName) {
                    const auto& hintColumn = ColumnsHint[i];
                    const auto& sourceColumn = sourceColumns->List[i];
                    if (hintColumn != sourceColumn) {
                        if (!mismatchFound) {
                            str << "Column names in SELECT don't match column specification in parenthesis";
                            mismatchFound = true;
                        }
                        str << ". \"" << hintColumn << "\" doesn't match \"" << sourceColumn << "\"";
                    }
                }
            }
            if (mismatchFound) {
                ctx.Warning(Pos, TIssuesIds::YQL_SOURCE_SELECT_COLUMN_MISMATCH) << str.Str();
            }
        }
        return true;
    }

    TNodePtr Build(TContext& ctx) override {
        auto input = Source->Build(ctx);
        if (ColumnsHint.empty() || !Source->GetColumns()) {
            return input;
        }
        auto srcColumn = Source->GetColumns()->List.begin();
        auto structObj = Y("AsStruct");
        for (auto column: ColumnsHint) {
            structObj = L(structObj, Q(Y(BuildQuotedAtom(Pos, column),
                Y("Member", "row", BuildQuotedAtom(Pos, *srcColumn))
            )));
            ++srcColumn;
        }
        return Y("OrderedMap", input, BuildLambda(Pos, Y("row"), structObj));
    }

    TNodePtr DoClone() const final {
        return new TModifyBySource(Pos, OperationHumanName, ColumnsHint, Source->CloneSource());
    }

    bool IsOrdered() const final {
        return Source->IsOrdered();
    }

private:
    TString OperationHumanName;
    TSourcePtr Source;
};

TSourcePtr BuildWriteValues(TPosition pos, const TString& operationHumanName, const TVector<TString>& columnsHint, const TVector<TVector<TNodePtr>>& values) {
    return new TModifyByValues(pos, operationHumanName, columnsHint, values);
}

TSourcePtr BuildWriteValues(TPosition pos, const TString& operationHumanName, const TVector<TString>& columnsHint, const TVector<TNodePtr>& values) {
    return new TModifyByValues(pos, operationHumanName, columnsHint, {values});
}

TSourcePtr BuildWriteValues(TPosition pos, const TString& operationHumanName, const TVector<TString>& columnsHint, TSourcePtr source) {
    return new TModifyBySource(pos, operationHumanName, columnsHint, std::move(source));
}

TSourcePtr BuildUpdateValues(TPosition pos, const TVector<TString>& columnsHint, const TVector<TNodePtr>& values) {
    return new TUpdateByValues(pos, "UPDATE", columnsHint, values);
}

class TWriteColumnsNode: public TAstListNode {
public:
    TWriteColumnsNode(TPosition pos, const TTableRef& table, EWriteColumnMode mode, TSourcePtr values = nullptr, TNodePtr options = nullptr)
        : TAstListNode(pos)
        , Table(table)
        , Mode(mode)
        , Values(std::move(values))
        , Options(std::move(options))
    {
        FakeSource = BuildFakeSource(pos);
    }

    void ResetSource(TSourcePtr source) {
        TableSource = std::move(source);
    }

    void ResetUpdate(TSourcePtr update) {
        Update = std::move(update);
    }

    bool DoInit(TContext& ctx, ISource* src) override {
        if (!Table.Check(ctx)) {
            return false;
        }

        TTableList tableList;
        TNodePtr values;
        auto options = Y();
        if (Options) {
            if (!Options->Init(ctx, src)) {
                return false;
            }
            options = L(Options);
        }

        ISource* underlyingSrc = src;

        if (TableSource) {
            ctx.PushBlockShortcuts();
            if (!TableSource->Init(ctx, src) || !TableSource->InitFilters(ctx)) {
                return false;
            }
            options = L(options, Q(Y(Q("filter"), TableSource->BuildFilterLambda(ctx.GroundBlockShortcuts(Pos)))));
        }

        bool unordered = false;
        ctx.PushBlockShortcuts();
        if (Values) {
            if (!Values->Init(ctx, TableSource.Get())) {
                return false;
            }

            Values->GetInputTables(tableList);
            underlyingSrc = Values.Get();
            values = Values->Build(ctx);
            if (!values) {
                return false;
            }
            unordered = !Values->IsOrdered();
        }

        TNodePtr node(BuildInputTables(Pos, tableList, false));
        if (!node->Init(ctx, underlyingSrc)) {
            return false;
        }

        if (Update) {
            if (!Update->Init(ctx, TableSource.Get()) || !Update->InitFilters(ctx)) {
                return false;
            }
            options = L(options, Q(Y(Q("update"), Update->Build(ctx))));
        }

        auto write = BuildWriteTable(Pos, "values", Table, Mode, std::move(options));
        if (!write->Init(ctx, FakeSource.Get())) {
            return false;
        }
        node = ctx.GroundBlockShortcuts(Pos, node);
        if (values) {
            node = L(node, Y("let", "values", values));
            if (unordered && ctx.UseUnordered(Table)) {
                node = L(node, Y("let", "values", Y("Unordered", "values")));
            }
        } else {
            node = L(node, Y("let", "values", Y("Void")));
        }
        node = L(node, Y("let", "world", write));
        node = L(node, Y("return", "world"));

        Add("block", Q(node));
        return true;
    }

    TNodePtr DoClone() const final {
        return {};
    }

protected:
    TTableRef Table;
    TSourcePtr TableSource;
    EWriteColumnMode Mode;
    TSourcePtr Values;
    TSourcePtr Update;
    TSourcePtr FakeSource;
    TNodePtr Options;
};

EWriteColumnMode ToWriteColumnsMode(ESQLWriteColumnMode sqlWriteColumnMode) {
    return sqlIntoMode2WriteColumn.at(sqlWriteColumnMode);
}

TNodePtr BuildWriteColumns(TPosition pos, const TTableRef& table, EWriteColumnMode mode, TSourcePtr values, TNodePtr options) {
    YQL_ENSURE(values, "Invalid values node");
    return new TWriteColumnsNode(pos, table, mode, std::move(values), std::move(options));
}

TNodePtr BuildUpdateColumns(TPosition pos, const TTableRef& table, TSourcePtr values, TSourcePtr source) {
    YQL_ENSURE(values, "Invalid values node");
    TIntrusivePtr<TWriteColumnsNode> writeNode = new TWriteColumnsNode(pos, table, EWriteColumnMode::Update);
    writeNode->ResetSource(std::move(source));
    writeNode->ResetUpdate(std::move(values));
    return writeNode;
}

TNodePtr BuildDelete(TPosition pos, const TTableRef& table, TSourcePtr source) {
    TIntrusivePtr<TWriteColumnsNode> writeNode = new TWriteColumnsNode(pos, table, EWriteColumnMode::Delete);
    writeNode->ResetSource(std::move(source));
    return writeNode;
}


class TEraseColumnsNode: public TAstListNode {
public:
    TEraseColumnsNode(TPosition pos, const TVector<TString>& columns)
        : TAstListNode(pos)
        , Columns(columns)
    {
    }

    bool DoInit(TContext& ctx, ISource* src) override {
        Y_UNUSED(ctx);
        Y_UNUSED(src);

        TNodePtr columnList = Y();
        for (const auto& column: Columns) {
            columnList->Add(Q(column));
        }

        Add(Q(Y(Q("erase_columns"), Q(columnList))));

        return true;
    }

    TNodePtr DoClone() const final {
        return {};
    }

private:
    TVector<TString> Columns;
};


TNodePtr BuildEraseColumns(TPosition pos, const TVector<TString>& columns) {
    return new TEraseColumnsNode(pos, columns);
}

} // namespace NSQLTranslationV0
