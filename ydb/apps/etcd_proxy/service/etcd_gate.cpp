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

using TActorsList = std::vector<TActorId>;

class TBatch : public TActorBootstrapped<TBatch>
{
public:
    TBatch(const i64 revision, TActorsList&& actors, TSharedStuff::TPtr stuff)
        : Revision(revision), Actors(std::move(actors)), Stuff(std::move(stuff))
    {
        Operations.reserve(Actors.size());
    }

    void Bootstrap(const TActorContext& ctx) {
        if (Actors.empty())
            return Die(ctx);


        const auto guard = std::shared_ptr<TBatch>(this, [my = this->SelfId(), stuff = TSharedStuff::TWeakPtr(Stuff)](void*) {
            if (const auto lock = stuff.lock())
                lock->ActorSystem->Send(my, new TEvents::TEvWakeup);
        });

        for (const auto& actor : Actors)
            ctx.Send(actor, new TEvGiveOperation(std::bind(&TBatch::AddOperation, this, std::placeholders::_1), guard));
        this->Become(&TBatch::CollectOperations);
    }
private:
    TQueryClient::TQueryResultFunc GetQueryResultFunc() {
        size_t resultsCounter = 0U, paramsCounter = 0U;
        NYdb::TParamsBuilder params;
        const auto& revisionParamName = AddParam("Revision", params, Revision);
        std::ostringstream sql;
        sql << Stuff->TablePrefix;
        sql << "-- Batch >>>> " << Operations.size() << std::endl;
        for (const auto& operation : Operations)
            operation(sql, params, &paramsCounter, &resultsCounter);
        sql << "insert into `commited` (`revision`,`timestamp`) values (" << revisionParamName << ",CurrentUtcDatetime());" << std::endl;
        sql << "-- Batch <<<< " << Operations.size() << std::endl;
//      std::cout << std::endl << sql.view() << std::endl;

        return [query = sql.str(), args = params.Build()](TQueryClient::TSession session) -> TAsyncExecuteQueryResult {
            return session.ExecuteQuery(query, TTxControl::BeginTx().CommitTx(), args);
        };
    }

    void SendDatabaseRequest() {
        this->Become(&TBatch::WaitResultFunc);
        Stuff->Client->RetryQuery(GetQueryResultFunc()).Subscribe([my = this->SelfId(), stuff = TSharedStuff::TWeakPtr(Stuff)](const auto& future) {
            if (const auto lock = stuff.lock()) {
                if (const auto res = future.GetValue(); res.IsSuccess())
                    lock->ActorSystem->Send(my, new TEvQueryResult(res.GetResultSets()));
                else
                    lock->ActorSystem->Send(my, new TEvQueryError(res.GetIssues()));
            }
        });
    }

    STFUNC(CollectOperations) {
        switch (ev->GetTypeRewrite()) {
            cFunc(TEvents::TEvWakeup::EventType, SendDatabaseRequest);
        }
    }

    STFUNC(WaitResultFunc) {
        switch (ev->GetTypeRewrite()) {
            HFunc(TEvQueryResult, Handle);
            HFunc(TEvQueryError, Handle);
        }
    }

    void AddOperation(TGenerator&& generator) {
        Operations.emplace_back(std::move(generator));
    }

    void Handle(TEvQueryResult::TPtr &ev, const TActorContext& ctx) {
        for (const auto& actor : Actors)
            ctx.Send(actor, new TEvQueryResult(ev->Get()->Results));
        Die(ctx);
    }

    void Handle(TEvQueryError::TPtr &ev, const TActorContext& ctx) {
        for (const auto& actor : Actors)
            ctx.Send(actor, new TEvQueryError(ev->Get()->Issues));
        Die(ctx);
    }
private:
    const i64 Revision;
    const TActorsList Actors;
    const TSharedStuff::TPtr Stuff;

    std::vector<TGenerator> Operations;
};

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
    std::deque<std::tuple<TKeysSet, TActorsList>> Queue;

    STFUNC(StateFunc) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvRequestRevision, Handle);

            HFunc(TEvQueryResult, Handle);
            HFunc(TEvQueryError, Handle);
        }
    }

    static bool HasIntersection(const TKeysSet& lhs, const TKeysSet& rhs) {
        for (auto i = lhs.cbegin(), j = rhs.cbegin(); lhs.cend() != i && rhs.cend() != j;) {
            if (*i < *j) {
                if (!i->second.empty() && (Endless == i->second || i->second >= j->first))
                    return true;
                else
                    i = lhs.lower_bound(*j);
            } else if (*i > *j) {
                if (!j->second.empty() && (Endless == j->second || j->second >= i->first))
                    return true;
                else
                    j = rhs.lower_bound(*i);
            } else
                return true;
        }

        return false;
    }

    void Handle(TEvRequestRevision::TPtr &ev) {
        if (Queue.empty()) {
            Queue.emplace_back(std::move(ev->Get()->KeysSet), TActorsList(1U, ev->Sender));
            RequestNextRevision();
        } else {
            if (!ev->Get()->KeysSet.empty()) {
                for (auto it = Queue.begin(); Queue.end() > it; ++it) {
                    if (auto& keys = std::get<TKeysSet>(*it); !keys.empty() && (!BatchLimit || BatchLimit > std::get<TActorsList>(*it).size()) && !HasIntersection(ev->Get()->KeysSet, keys)) {
                        keys.merge(std::move(ev->Get()->KeysSet));
                        std::get<TActorsList>(*it).emplace_back(ev->Sender);
                        return;
                    }
                }
            }
            Queue.emplace_back(std::move(ev->Get()->KeysSet), TActorsList(1U, ev->Sender));
        }
    }

    void Handle(TEvQueryResult::TPtr &ev, const TActorContext& ctx) {
        i64 revision = 0ULL;
        if (auto parser = NYdb::TResultSetParser(ev->Get()->Results.front()); parser.TryNextRow()) {
            revision = NYdb::TValueParser(parser.GetValue(0)).GetInt64();
        }

        if (auto actors = std::get<TActorsList>(std::move(Queue.front())); 1U == actors.size())
            ctx.Send(actors.front(), new TEvReturnRevision(revision));
        else
            ctx.RegisterWithSameMailbox(new TBatch(revision, std::move(actors), Stuff));

        Queue.pop_front();
        RequestNextRevision();
    }

    void Handle(TEvQueryError::TPtr &ev, const TActorContext& ctx) {
        std::cout << "Get next revision SQL error received " << ev->Get()->Issues.ToString() << std::endl;
        for (const auto& sender : std::get<TActorsList>(std::move(Queue.front())))
            ctx.Send(sender, new TEvQueryError(ev->Get()->Issues));
        Queue.pop_front();
        RequestNextRevision();
    }

    void RequestNextRevision() {
        if (Queue.empty())
            return;

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
};

}

NActors::IActor* BuildMainGate(TIntrusivePtr<NMonitoring::TDynamicCounters> counters, TSharedStuff::TPtr stuff) {
    return new TMainGate(std::move(counters), std::move(stuff));

}

}
