#include "etcd_gate.h"
#include "etcd_shared.h"
#include "etcd_events.h"
#include "etcd_impl.h"

#include <ydb/library/actors/core/actor_bootstrapped.h>
#include <ydb/library/actors/core/executor_thread.h>

namespace NEtcd {

using namespace NActors;
using namespace NYdb::NQuery;

namespace {

class TMainGate : public TActorBootstrapped<TMainGate> {
public:
    TMainGate(TIntrusivePtr<NMonitoring::TDynamicCounters> counters, TSharedStuff::TPtr stuff)
        : Counters(std::move(counters)), Stuff(std::move(stuff)), Query(Stuff->TablePrefix + NResource::Find("revision.sql"sv))
    {}

    void Bootstrap(const TActorContext&) {
        Become(&TThis::StateFunc);
        Stuff->MainGate = SelfId();
    }
private:
    std::deque<std::tuple<TKeysSet, std::vector<TActorId>>> Queue;

    STFUNC(StateFunc) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvRequestRevision, Handle);

            HFunc(TEvQueryResult, Handle);
            HFunc(TEvQueryError, Handle);
        }
    }

    void Handle(TEvRequestRevision::TPtr &ev) {
        const bool send = Queue.empty() && Current.empty();
        Queue.emplace_back(ev->Get()->KeysSet, std::vector<TActorId>(1U, ev->Sender));
        if (send)
            RequestNextRevision();
    }

    void Handle(TEvQueryResult::TPtr &ev, const TActorContext& ctx) {
        i64 revision = 0ULL;
        if (auto parser = NYdb::TResultSetParser(ev->Get()->Results.front()); parser.TryNextRow()) {
            revision = NYdb::TValueParser(parser.GetValue(0)).GetInt64();
        }

        for (const auto& sender : Current)
            ctx.Send(sender, new TEvReturnRevision(revision));
        Current.clear();
        RequestNextRevision();
    }

    void Handle(TEvQueryError::TPtr &ev, const TActorContext& ctx) {
        std::cout << "Get next revision SQL error received " << ev->Get()->Issues.ToString() << std::endl;

        for (const auto& sender : Current)
            ctx.Send(sender, new TEvQueryError(ev->Get()->Issues));
        Current.clear();
        RequestNextRevision();
    }

    void RequestNextRevision() {
        if (Queue.empty())
            return;

        Current = std::get<std::vector<TActorId>>(std::move(Queue.front()));
        Queue.pop_front();

        TQueryClient::TQueryResultFunc callback = [query = Query](TQueryClient::TSession session) -> TAsyncExecuteQueryResult {
            return session.ExecuteQuery(query, TTxControl::BeginTx().CommitTx());
        };

        Stuff->Client->RetryQuery(std::move(callback)).Subscribe([my = this->SelfId(), stuff = TSharedStuff::TWeakPtr(Stuff)](const auto& future) {
            if (const auto lock = stuff.lock()) {
                if (const auto res = future.GetValueSync(); res.IsSuccess())
                    lock->ActorSystem->Send(my, new TEvQueryResult(res.GetResultSets()));
                else
                    lock->ActorSystem->Send(my, new TEvQueryError(res.GetIssues()));
            }
        });
    }

    const TIntrusivePtr<NMonitoring::TDynamicCounters> Counters;
    const TSharedStuff::TPtr Stuff;
    const std::string Query;

    std::vector<TActorId> Current;
};

}

NActors::IActor* BuildMainGate(TIntrusivePtr<NMonitoring::TDynamicCounters> counters, TSharedStuff::TPtr stuff) {
    return new TMainGate(std::move(counters), std::move(stuff));

}

}
