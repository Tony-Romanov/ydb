#include "ydb_service_operation.h"

#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/export/export.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/import/import.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/table/table.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/query/query.h>
#include <ydb/public/lib/ydb_cli/common/print_operation.h>

#include <util/string/builder.h>

namespace NYdb {
namespace NConsoleClient {

using namespace NKikimr::NOperationId;

namespace {

    template <typename T>
    int GetOperation(NOperation::TOperationClient& client, const TOperationId& id, EDataFormat format) {
        T operation = client.Get<T>(id).GetValueSync();
        PrintOperation(operation, format);
        if (!operation.Ready()) {
            return EXIT_SUCCESS;
        }
        switch (operation.Status().GetStatus()) {
        case EStatus::SUCCESS:
            return EXIT_SUCCESS;
        default:
            return EXIT_FAILURE;
        }
    }

    template <typename T>
    void ListOperations(NOperation::TOperationClient& client, ui64 pageSize, const TString& pageToken, EDataFormat format) {
        NOperation::TOperationsList<T> operations = client.List<T>(pageSize, pageToken).GetValueSync();
        NStatusHelpers::ThrowOnErrorOrPrintIssues(operations);
        PrintOperationsList(operations, format);
    }

} // anonymous

TCommandOperation::TCommandOperation()
    : TClientCommandTree("operation", {}, "Operation service operations")
{
    AddCommand(std::make_unique<TCommandGetOperation>());
    AddCommand(std::make_unique<TCommandCancelOperation>());
    AddCommand(std::make_unique<TCommandForgetOperation>());
    AddCommand(std::make_unique<TCommandListOperations>());
}

void TCommandWithOperationId::Config(TConfig& config) {
    TYdbCommand::Config(config);

    config.SetFreeArgsNum(1);
    SetFreeArgTitle(0, "<id>", "Operation ID");
}

void TCommandWithOperationId::Parse(TConfig& config) {
    TYdbCommand::Parse(config);

    try {
        OperationId = TOperationId(config.ParseResult->GetFreeArgs()[0]);
    } catch (const yexception& ex) {
        throw TMisuseException() << "Invalid operation ID";
    }
}

TCommandGetOperation::TCommandGetOperation()
    : TCommandWithOperationId("get", {}, "Check status for a given operation")
{
}

void TCommandGetOperation::Config(TConfig& config) {
    TCommandWithOperationId::Config(config);
    AddDeprecatedJsonOption(config);
    AddOutputFormats(config, { EDataFormat::Pretty, EDataFormat::ProtoJsonBase64 });
    config.Opts->MutuallyExclusive("json", "format");
}

void TCommandGetOperation::Parse(TConfig& config) {
    TCommandWithOperationId::Parse(config);
    ParseOutputFormats();
}

int TCommandGetOperation::Run(TConfig& config) {
    NOperation::TOperationClient client(CreateDriver(config));

    switch (OperationId.GetKind()) {
    case TOperationId::EXPORT:
        if (OperationId.GetSubKind() == "s3") {
            return GetOperation<NExport::TExportToS3Response>(client, OperationId, OutputFormat);
        } else { // fallback to "yt"
            return GetOperation<NExport::TExportToYtResponse>(client, OperationId, OutputFormat);
        }
    case TOperationId::IMPORT:
        if (OperationId.GetSubKind() == "s3") {
            return GetOperation<NImport::TImportFromS3Response>(client, OperationId, OutputFormat);
        } else {
            throw TMisuseException() << "Invalid operation ID (unexpected sub-kind of operation)";
        }
    case TOperationId::BUILD_INDEX:
        return GetOperation<NTable::TBuildIndexOperation>(client, OperationId, OutputFormat);
    case TOperationId::SCRIPT_EXECUTION:
        return GetOperation<NQuery::TScriptExecutionOperation>(client, OperationId, OutputFormat);
    default:
        throw TMisuseException() << "Invalid operation ID (unexpected kind of operation)";
    }

    return EXIT_SUCCESS;
}

TCommandCancelOperation::TCommandCancelOperation()
    : TCommandWithOperationId("cancel", {}, "Start cancellation of a long-running operation")
{
}

int TCommandCancelOperation::Run(TConfig& config) {
    NOperation::TOperationClient client(CreateDriver(config));
    NStatusHelpers::ThrowOnErrorOrPrintIssues(client.Cancel(OperationId).GetValueSync());
    return EXIT_SUCCESS;
}

TCommandForgetOperation::TCommandForgetOperation()
    : TCommandWithOperationId("forget", {}, "Forget long-running operation")
{
}

int TCommandForgetOperation::Run(TConfig& config) {
    NOperation::TOperationClient client(CreateDriver(config));
    NStatusHelpers::ThrowOnErrorOrPrintIssues(client.Forget(OperationId).GetValueSync());
    return EXIT_SUCCESS;
}

void TCommandListOperations::InitializeKindToHandler(TConfig& config) {
    KindToHandler = {
        {"export/s3", &ListOperations<NExport::TExportToS3Response>},
        {"import/s3", &ListOperations<NImport::TImportFromS3Response>},
        {"buildindex", &ListOperations<NTable::TBuildIndexOperation>},
        {"scriptexec", &ListOperations<NQuery::TScriptExecutionOperation>},
    };
    if (config.UseExportToYt) {
        KindToHandler.emplace("export", THandlerWrapper(&ListOperations<NExport::TExportToYtResponse>, true)); // deprecated
        KindToHandler.emplace("export/yt", &ListOperations<NExport::TExportToYtResponse>);
    }
}

TString TCommandListOperations::KindChoices() {
    TStringBuilder help;

    bool first = true;
    for (const auto& [kind, handler] : KindToHandler) {
        if (handler.Hidden) {
            continue;
        }
        if (!first) {
            help << ", ";
        }
        help << kind;
        first = false;
    }

    return help;
}

TCommandListOperations::TCommandListOperations()
    : TYdbCommand("list", {}, "List operations of specified kind")
{
}

void TCommandListOperations::Config(TConfig& config) {
    TYdbCommand::Config(config);

    InitializeKindToHandler(config);

    config.Opts->AddLongOption('s', "page-size", "Page size")
        .RequiredArgument("NUM").StoreResult(&PageSize);
    config.Opts->AddLongOption('t', "page-token", "Page token")
        .RequiredArgument("STRING").StoreResult(&PageToken);
    AddDeprecatedJsonOption(config);
    AddOutputFormats(config, { EDataFormat::Pretty, EDataFormat::ProtoJsonBase64 });
    config.Opts->MutuallyExclusive("json", "format");

    config.SetFreeArgsNum(1);
    SetFreeArgTitle(0, "<kind>", KindChoices());
}

void TCommandListOperations::Parse(TConfig& config) {
    TYdbCommand::Parse(config);
    ParseOutputFormats();

    Kind = config.ParseResult->GetFreeArgs()[0];
    if (!KindToHandler.contains(Kind)) {
        throw TMisuseException() << "Invalid kind. Use one of: " << KindChoices();
    }
}

int TCommandListOperations::Run(TConfig& config) {
    NOperation::TOperationClient client(CreateDriver(config));
    KindToHandler.at(Kind)(client, PageSize, PageToken, OutputFormat);
    return EXIT_SUCCESS;
}

}
}
