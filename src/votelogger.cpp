/*
 * votelogger.cpp
 *
 *  Created on: Dec 31, 2017
 *      Author: nullifiedcat
 */

#include "common.hpp"
#include <boost/algorithm/string.hpp>
#include <settings/Bool.hpp>
#include "CatBot.hpp"
#include "votelogger.hpp"

static settings::Boolean vote_kicky{ "votelogger.autovote.yes", "false" };
static settings::Boolean vote_kickn{ "votelogger.autovote.no", "false" };
static settings::Boolean vote_rage_vote{ "votelogger.autovote.no.rage", "false" };
static settings::Boolean vote_always_no{ "votelogger.autovote.always-no", "false" };
static settings::Boolean vote_yes_on_non_friends{ "votelogger.autovote.yes-on-non-friends", "true" };
static settings::Boolean vote_no_on_friends{ "votelogger.autovote.no-on-friends", "true" };
static settings::Boolean chat{ "votelogger.chat", "true" };
static settings::Boolean chat_partysay{ "votelogger.chat.partysay", "false" };
static settings::Boolean chat_casts{ "votelogger.chat.casts", "false" };
static settings::Boolean chat_casts_f1_only{ "votelogger.chat.casts.f1-only", "true" };
// Leave party and crash, useful for personal party bots
static settings::Boolean abandon_and_crash_on_kick{ "votelogger.restart-on-kick", "false" };

namespace votelogger
{

static bool was_local_player{ false };
static Timer local_kick_timer{};
static int kicked_player;

static void vote_rage_back()
{
    static Timer attempt_vote_time;
    char cmd[40];
    player_info_s info;
    std::vector<int> targets;

    if (!g_IEngine->IsInGame() || !attempt_vote_time.test_and_set(1000))
        return;

    for (auto const &ent : entity_cache::player_cache)
    {
        // TO DO: m_bEnemy check only when you can't vote off players from the opposite team
        if (ent == LOCAL_E || ent->m_bEnemy())
            continue;

        if (!GetPlayerInfo(ent->m_IDX, &info))
            continue;

        auto &pl = playerlist::AccessData(info.friendsID);
        if (pl.state == playerlist::k_EState::RAGE)
            targets.emplace_back(info.userID);
    }
    if (targets.empty())
        return;

    std::snprintf(cmd, sizeof(cmd), "callvote kick \"%d cheating\"", targets[UniformRandomInt(0, targets.size() - 1)]);
    g_IEngine->ClientCmd_Unrestricted(cmd);
}
// Call Vote RNG
struct CVRNG
{
    std::string content;
    unsigned time;
    Timer timer{};
};
static CVRNG vote_command;
void dispatchUserMessage(bf_read &buffer, int type)
{
    switch (type)
    {
    case 45:
    {
        // Vote setup Failed, Refresh vote timer for catbot so it can try again
        int reason   = buffer.ReadByte();
        int cooldown = buffer.ReadShort();
        int delay    = 4;

        if (reason == 2) // VOTE_FAILED_RATE_EXCEEDED
            delay = cooldown;

        hacks::shared::catbot::timer_votekicks.last -= std::chrono::seconds(delay);
        break;
    }
    case 46:
    {
        was_local_player = false;
        int team         = buffer.ReadByte();
        int vote_id      = buffer.ReadLong();
        int caller       = buffer.ReadByte();
        char reason[64];
        char name[64];
        buffer.ReadString(reason, 64, false, nullptr);
        buffer.ReadString(name, 64, false, nullptr);
        auto target = (unsigned char) buffer.ReadByte();
        buffer.Seek(0);
        target >>= 1;

        // info is the person getting kicked,
        // info2 is the person calling the kick.
        player_info_s info{}, info2{};
        if (!GetPlayerInfo(target, &info) || !GetPlayerInfo(caller, &info2))
            break;

        kicked_player = target;
        logging::Info("Vote called to kick %s [U:1:%u] for %s by %s [U:1:%u]", info.name, info.friendsID, reason, info2.name, info2.friendsID);
        if (info.friendsID == g_ISteamUser->GetSteamID().GetAccountID())
        {
            was_local_player = true;
            local_kick_timer.update();
        }

        //  always vote no
        if (*vote_always_no)
        {
            vote_command = { strfmt("vote %d option2", vote_id).get(), 0 };
            vote_command.timer.update();
            break;
        }

        // vote no on friends
        if (*vote_no_on_friends)
        {
            auto &pl = playerlist::AccessData(info.friendsID);
            // Check if target is a local IPC bot
            bool is_local_bot = false;
            if (ipc::peer && ipc::peer->memory)
            {
                for (unsigned i = 0; i < cat_ipc::max_peers; i++)
                {
                    if (!ipc::peer->memory->peer_data[i].free && 
                        ipc::peer->memory->peer_user_data[i].friendid == info.friendsID)
                    {
                        is_local_bot = true;
                        break;
                    }
                }
            }
            if (pl.state == playerlist::k_EState::FRIEND || pl.state == playerlist::k_EState::CAT || is_local_bot)
            {
                vote_command = { strfmt("vote %d option2", vote_id).get(), 0 };
                vote_command.timer.update();
                break;
            }
        }

        // vote yes on non-friends
        if (*vote_yes_on_non_friends)
        {
            auto &pl = playerlist::AccessData(info.friendsID);
            if (pl.state != playerlist::k_EState::FRIEND && pl.state != playerlist::k_EState::CAT)
            {
                // Check if target is a local IPC bot
                bool is_local_bot = false;
                if (ipc::peer && ipc::peer->memory)
                {
                    for (unsigned i = 0; i < cat_ipc::max_peers; i++)
                    {
                        if (!ipc::peer->memory->peer_data[i].free && 
                            ipc::peer->memory->peer_user_data[i].friendid == info.friendsID)
                        {
                            is_local_bot = true;
                            break;
                        }
                    }
                }
                
             
                if (!is_local_bot)
                {
                    vote_command = { strfmt("vote %d option1", vote_id).get(), 0 };
                    vote_command.timer.update();
                }
                break;
            }
        }

        if (*vote_kickn || *vote_kicky)
        {
            using namespace playerlist;

            auto &pl             = AccessData(info.friendsID);
            auto &pl_caller      = AccessData(info2.friendsID);
            bool friendly_kicked = pl.state != k_EState::RAGE && pl.state != k_EState::DEFAULT;
            bool friendly_caller = pl_caller.state != k_EState::RAGE && pl_caller.state != k_EState::DEFAULT;

            if (*vote_kickn && friendly_kicked)
            {
                vote_command = { strfmt("vote %d option2", vote_id).get(), 0 };
                vote_command.timer.update();
                if (*vote_rage_vote && !friendly_caller)
                    pl_caller.state = k_EState::RAGE;
            }
            else if (*vote_kicky && !friendly_kicked)
            {
                vote_command = { strfmt("vote %d option1", vote_id).get(), 0 };
                vote_command.timer.update();
            }
        }
        if (*chat_partysay)
        {
            char formated_string[256];
            std::snprintf(formated_string, sizeof(formated_string), "[CAT] votekick called: %s => %s (%s)", info2.name, info.name, reason);
            if (chat_partysay)
                re::CTFPartyClient::GTFPartyClient()->SendPartyChat(formated_string);
        }
#if ENABLE_VISUALS
        if (chat)
            PrintChat("Votekick called: \x07%06X%s\x01 => \x07%06X%s\x01 (%s)", colors::chat::team(g_pPlayerResource->getTeam(caller)), info2.name, colors::chat::team(g_pPlayerResource->getTeam(target)), info.name, reason);
#endif
        break;
    }
    case 47:
    {
        logging::Info("Vote passed");
        // if (was_local_player && requeue)
        //    tfmm::startQueue();
        break;
    }
    case 48:
        logging::Info("Vote failed");
        break;
    case 49:
        logging::Info("VoteSetup?");
        break;
    default:
        break;
    }
}
static bool found_message = false;
void onShutdown(std::string message)
{
    if (message.find("Generic_Kicked") == message.npos)
    {
        found_message = false;
        return;
    }
    if (local_kick_timer.check(60000) || !was_local_player)
    {
        found_message = false;
        return;
    }
    if (abandon_and_crash_on_kick)
    {
        found_message = true;
        g_IEngine->ClientCmd_Unrestricted("tf_party_leave");
        local_kick_timer.update();
    }
    else
        found_message = false;
}

static void setup_paint_abandon()
{
    EC::Register(
        EC::Paint,
        []()
        {
            if (vote_command.content != "" && vote_command.timer.test_and_set(vote_command.time))
            {
                g_IEngine->ClientCmd_Unrestricted(vote_command.content.c_str());
                vote_command.content = "";
            }
            if (!found_message)
                return;
            if (local_kick_timer.check(60000) || !local_kick_timer.test_and_set(10000) || !was_local_player)
                return;
            if (abandon_and_crash_on_kick)
                *(int *) 0 = 0;
        },
        "vote_abandon_restart");
}

static void setup_vote_rage()
{
    EC::Register(EC::CreateMove, vote_rage_back, "vote_rage_back");
}

static void reset_vote_rage()
{
    EC::Unregister(EC::CreateMove, "vote_rage_back");
}

class VoteEventListener : public IGameEventListener
{
public:
    void FireGameEvent(KeyValues *event) override
    {
        if (!*chat_casts || (!*chat_partysay && !chat))
            return;
        const char *name = event->GetName();
        if (!strcmp(name, "vote_cast"))
        {
            bool vote_option = event->GetInt("vote_option");
            if (*chat_casts_f1_only && vote_option)
                return;
            int eid = event->GetInt("entityid");

            player_info_s info{};
            if (!GetPlayerInfo(eid, &info))
                return;
            if (chat_partysay)
            {
                char formated_string[256];
                std::snprintf(formated_string, sizeof(formated_string), "[CAT] %s [U:1:%u] %s", info.name, info.friendsID, vote_option ? "F2" : "F1");

                re::CTFPartyClient::GTFPartyClient()->SendPartyChat(formated_string);
            }
#if ENABLE_VISUALS
            if (chat)
                PrintChat("\x07%06X%s\x01 [U:1:%u] %s", colors::chat::team(g_pPlayerResource->getTeam(eid)), info.name, info.friendsID, vote_option ? "F2" : "F1");
#endif
        }
    }
};
static VoteEventListener listener{};
static InitRoutine init(
    []()
    {
        if (*vote_rage_vote)
            setup_vote_rage();
        setup_paint_abandon();

        vote_rage_vote.installChangeCallback(
            [](settings::VariableBase<bool> &var, bool new_val)
            {
                if (new_val)
                    setup_vote_rage();
                else
                    reset_vote_rage();
            });
        g_IGameEventManager->AddListener(&listener, false);
        EC::Register(
            EC::Shutdown, []() { g_IGameEventManager->RemoveListener(&listener); }, "event_shutdown_vote");
    });
} // namespace votelogger
