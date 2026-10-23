/**
 * @file pet_config_test.cpp
 * @brief The pet data parsers, offline: a document is valid or it is refused whole (PHASE3 3.1, 3.8, 3.9).
 */

#include <algorithm>
#include <chrono>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/domain/pet/pet_config.h"
#include "core/domain/pet/pet_package.h"

namespace {

using lens::core::pet::parseAccessories;
using lens::core::pet::parseAnchors;
using lens::core::pet::parseAnimations;
using lens::core::pet::Playbook;
using lens::core::pet::Role;
using nlohmann::json;

/// A complete animations document: every required role, stretch, the idle pool, and the blink entry.
json animationsDocument()
{
    return json{
        {"roles", {
                      {"idle", "idle"},
                      {"study", "study"},
                      {"thinking", "thinking"},
                      {"celebrate", "celebrate"},
                      {"encourage", "encourage"},
                      {"sleep", "sleep"},
                      {"click_react", "click_react"},
                      {"pickup", "pickup"},
                      {"look_around", "look_around"},
                      {"yawn", "yawn"},
                      {"stretch", "stretch"},
                  }},
        {"idlePool", json::array({"look_around", "stretch"})},
        {"actions", {
                        {"idle", {{"frames", 5}, {"fps", 5}, {"loop", true}, {"blink", true}}},
                        {"study", {{"frames", 6}, {"fps", 4}, {"loop", true}, {"blink", true}}},
                        {"thinking", {{"frames", 4}, {"fps", 3}, {"loop", true}, {"blink", true}}},
                        {"celebrate", {{"frames", 8}, {"fps", 10}, {"loop", false}}},
                        {"encourage", {{"frames", 5}, {"fps", 8}, {"loop", false}}},
                        {"sleep", {{"frames", 4}, {"fps", 3}, {"loop", true}}},
                        {"click_react", {{"frames", 4}, {"fps", 12}, {"loop", false}}},
                        {"pickup", {{"frames", 4}, {"fps", 6}, {"loop", true}}},
                        {"look_around", {{"frames", 6}, {"fps", 6}, {"loop", false}}},
                        {"yawn", {{"frames", 5}, {"fps", 6}, {"loop", false}, {"returnTo", "sleep"}}},
                        {"stretch", {{"frames", 6}, {"fps", 6}, {"loop", false}}},
                        {"blink", {{"frames", 3}, {"fps", 10}, {"loop", false}}},
                    }},
    };
}

/// Anchors for every action in an animations document: one zero point per frame.
json anchorsDocument(json const& animations)
{
    json doc = {{"actions", json::object()}};
    for (auto const& [name, entry] : animations.at("actions").items()) {
        if (name == "blink") continue;
        json perFrame = json::array();
        for (int i = 0; i < entry.at("frames").get<int>(); ++i) {
            perFrame.push_back({{"head", {0, 0}}, {"face", {0, 0}}, {"body", {0, 0}}});
        }
        doc["actions"][name] = perFrame;
    }
    return doc;
}

/// The prototype's seven accessories: three slots, sleep hides them all.
json accessoriesDocument()
{
    auto const visible = json::array({"idle", "study", "thinking", "celebrate", "encourage", "click_react", "pickup", "look_around", "yawn", "stretch"});
    return json{{"accessories", {
                                    {{"id", "hat"}, {"slot", "head"}, {"supportedActions", visible}},
                                    {{"id", "beanie"}, {"slot", "head"}, {"supportedActions", visible}},
                                    {{"id", "sprout"}, {"slot", "head"}, {"supportedActions", visible}},
                                    {{"id", "crown"}, {"slot", "head"}, {"supportedActions", visible}},
                                    {{"id", "glasses"}, {"slot", "face"}, {"supportedActions", visible}},
                                    {{"id", "sunglasses"}, {"slot", "face"}, {"supportedActions", visible}},
                                    {{"id", "bowtie"}, {"slot", "body"}, {"supportedActions", visible}},
                                }}};
}

/// The playbook of the complete animations document above.
Playbook dogPlaybook()
{
    return parseAnimations(animationsDocument()).playbook;
}

TEST(PetConfig, ParsesEveryActionWithItsPlayback)
{
    auto const playbook = dogPlaybook();
    ASSERT_EQ(playbook.actions.size(), 11u);
    EXPECT_EQ(playbook.actions.at("study").frames, 6);
    EXPECT_EQ(playbook.actions.at("study").fps, 4);
    EXPECT_TRUE(playbook.actions.at("study").loop);
    EXPECT_FALSE(playbook.actions.at("celebrate").returnTo.has_value());
}

TEST(PetConfig, EveryRoleIsMappedToItsAction)
{
    auto const playbook = dogPlaybook();
    EXPECT_EQ(playbook.roles.at(Role::Study), "study");
    EXPECT_EQ(playbook.roles.at(Role::ClickReact), "click_react");
    EXPECT_EQ(playbook.roles.at(Role::Stretch), "stretch");
}

TEST(PetConfig, IdlePoolIsReadInDocumentOrder)
{
    auto const playbook = dogPlaybook();
    EXPECT_EQ(playbook.idlePool, (std::vector<std::string>{"look_around", "stretch"}));
}

TEST(PetConfig, ThePoolAlsoCarriesEveryOneShotNoRoleNames)
{
    // An action no role names is one only the idle timer can reach, so the pool takes it in without the
    // document having to name it twice.
    auto document              = animationsDocument();
    document["actions"]["wag"] = {{"frames", 4}, {"fps", 6}, {"loop", false}};
    auto const playbook        = parseAnimations(document).playbook;

    EXPECT_EQ(playbook.idlePool, (std::vector<std::string>{"look_around", "stretch", "wag"}));
}

TEST(PetConfig, ThePoolLeavesOutAnActionARoleNames)
{
    // Until a role names it, the same action is an extra; once one does, it is that role's and the pool
    // must not offer it at random as well.
    auto document                = animationsDocument();
    document["actions"]["wag"]   = {{"frames", 4}, {"fps", 6}, {"loop", false}};
    document["roles"]["stretch"] = "wag";

    EXPECT_EQ(parseAnimations(document).playbook.idlePool, (std::vector<std::string>{"look_around", "stretch"}));
}

TEST(PetConfig, ThePoolLeavesOutALoopingActionNoRoleNames)
{
    // The pool's contract is one-shots, for the same reason the list refuses them: an idle action that
    // never ends is one the pet never leaves.
    auto document               = animationsDocument();
    document["actions"]["doze"] = {{"frames", 4}, {"fps", 3}, {"loop", true}};
    auto const playbook         = parseAnimations(document).playbook;

    EXPECT_EQ(playbook.idlePool, (std::vector<std::string>{"look_around", "stretch"}));
}

TEST(PetConfig, YawnReturnsToSleep)
{
    auto const playbook = dogPlaybook();
    ASSERT_TRUE(playbook.actions.at("yawn").returnTo.has_value());
    EXPECT_EQ(*playbook.actions.at("yawn").returnTo, Role::Sleep);
}

TEST(PetConfig, BlinkIsItsOwnEntryNotAnAction)
{
    auto const animations = parseAnimations(animationsDocument());
    EXPECT_EQ(animations.blink.frames, 3);
    EXPECT_EQ(animations.blink.fps, 10);
    EXPECT_FALSE(animations.blink.loop);
    EXPECT_EQ(animations.playbook.actions.count("blink"), 0u);
}

TEST(PetConfig, BlinkFlagIsReadPerAction)
{
    auto const playbook = dogPlaybook();
    EXPECT_TRUE(playbook.actions.at("idle").blinks);
    EXPECT_TRUE(playbook.actions.at("study").blinks);
    EXPECT_FALSE(playbook.actions.at("sleep").blinks);
}

TEST(PetConfig, ParsesHoverReactionsAndTheirTiming)
{
    using namespace std::chrono_literals;
    auto document     = animationsDocument();
    document["hover"] = {
        {"notice", "look_around"},
        {"dwell", "stretch"},
        {"noticeDelayMs", 650},
        {"dwellDelayMs", 3200},
        {"cooldownMs", 9000},
    };
    auto const animations = parseAnimations(document);
    ASSERT_TRUE(animations.hover.has_value());
    EXPECT_EQ(animations.hover->notice, "look_around");
    EXPECT_EQ(animations.hover->dwell, "stretch");
    EXPECT_EQ(animations.hover->noticeDelay, 650ms);
    EXPECT_EQ(animations.hover->dwellDelay, 3200ms);
    EXPECT_EQ(animations.hover->cooldown, 9000ms);
}

TEST(PetConfig, HoverIsOptionalAndItsDelaysDefault)
{
    using namespace std::chrono_literals;
    auto document         = animationsDocument();
    document["hover"]     = {{"notice", "look_around"}};
    auto const animations = parseAnimations(document);
    ASSERT_TRUE(animations.hover.has_value());
    EXPECT_EQ(animations.hover->notice, "look_around");
    EXPECT_FALSE(animations.hover->dwell.has_value());
    EXPECT_EQ(animations.hover->noticeDelay, 350ms);
    EXPECT_EQ(animations.hover->dwellDelay, 2600ms);
    EXPECT_EQ(animations.hover->cooldown, 8000ms);

    document.erase("hover");
    EXPECT_FALSE(parseAnimations(document).hover.has_value());
}

TEST(PetConfig, HoverNeedsANoticeAndADwellNeedsANotice)
{
    auto document     = animationsDocument();
    document["hover"] = json::object();
    EXPECT_THROW(parseAnimations(document), std::runtime_error);

    document["hover"] = {{"dwell", "stretch"}};
    EXPECT_THROW(parseAnimations(document), std::runtime_error);
}

TEST(PetConfig, HoverActionsMustExistAndBeOneShot)
{
    auto document     = animationsDocument();
    document["hover"] = {{"notice", "no_such_action"}};
    EXPECT_THROW(parseAnimations(document), std::runtime_error);

    document["hover"] = {{"notice", "idle"}};
    EXPECT_THROW(parseAnimations(document), std::runtime_error);
}

TEST(PetConfig, HoverDelaysStayWithinTheirLimit)
{
    auto document     = animationsDocument();
    document["hover"] = {{"notice", "look_around"}, {"noticeDelayMs", -1}};
    EXPECT_THROW(parseAnimations(document), std::runtime_error);

    document["hover"] = {{"notice", "look_around"}, {"cooldownMs", 60'001}};
    try {
        parseAnimations(document);
        FAIL() << "a cooldown above the limit must be refused";
    } catch (lens::core::pet::Refused const& refused) {
        EXPECT_EQ(refused.reason(), lens::core::pet::Refusal::TooLarge);
    }
}

TEST(PetConfig, ParsesTheMovementBlock)
{
    auto document                    = animationsDocument();
    document["actions"]["walk"]      = {{"frames", 6}, {"fps", 8}, {"loop", true}};
    document["actions"]["edge_peek"] = {{"frames", 6}, {"fps", 5}, {"loop", true}};
    document["movement"]             = {
        {"style", "trot"},
        {"walk", "walk"},
        {"peek", "edge_peek"},
        {"range", 32},
    };

    auto const animations = parseAnimations(document);
    ASSERT_TRUE(animations.movement.has_value());
    EXPECT_EQ(animations.movement->style, lens::core::pet::MovementStyle::Trot);
    EXPECT_EQ(animations.movement->walk, "walk");
    EXPECT_EQ(animations.movement->peek, "edge_peek");
    EXPECT_EQ(animations.movement->range, 32);
}

TEST(PetConfig, MovementIsOptionalAndItsRadiusDefaults)
{
    // A package written before movement existed keeps loading and never wanders.
    auto document               = animationsDocument();
    document["actions"]["walk"] = {{"frames", 6}, {"fps", 8}, {"loop", true}};
    auto const without          = parseAnimations(document);
    EXPECT_FALSE(without.movement.has_value());

    document["movement"]  = {{"style", "glide"}, {"walk", "walk"}};
    auto const animations = parseAnimations(document);
    ASSERT_TRUE(animations.movement.has_value());
    EXPECT_FALSE(animations.movement->peek.has_value());
    EXPECT_EQ(animations.movement->range, 30);
}

TEST(PetConfig, RefusesAnUnknownMovementStyle)
{
    auto document               = animationsDocument();
    document["actions"]["walk"] = {{"frames", 6}, {"fps", 8}, {"loop", true}};
    document["movement"]        = {{"style", "swim"}, {"walk", "walk"}};
    EXPECT_THROW(parseAnimations(document), std::runtime_error);

    document["movement"] = {{"walk", "walk"}};
    EXPECT_THROW(parseAnimations(document), std::runtime_error);
}

TEST(PetConfig, MovementNeedsALoopingWalkAndPeek)
{
    auto document        = animationsDocument();
    document["movement"] = {{"style", "hop"}, {"walk", "celebrate"}};
    EXPECT_THROW(parseAnimations(document), std::runtime_error);

    document["actions"]["walk"] = {{"frames", 6}, {"fps", 8}, {"loop", true}};
    document["movement"]        = {{"style", "hop"}, {"walk", "walk"}, {"peek", "celebrate"}};
    EXPECT_THROW(parseAnimations(document), std::runtime_error);

    document["movement"] = {{"style", "hop"}, {"walk", "missing"}};
    EXPECT_THROW(parseAnimations(document), std::runtime_error);
}

TEST(PetConfig, MovementRadiusStaysBetweenTenAndSixty)
{
    auto document               = animationsDocument();
    document["actions"]["walk"] = {{"frames", 6}, {"fps", 8}, {"loop", true}};

    for (auto const range : {10, 60}) {
        document["movement"] = {{"style", "float"}, {"walk", "walk"}, {"range", range}};
        EXPECT_EQ(parseAnimations(document).movement->range, range);
    }

    document["movement"] = {{"style", "float"}, {"walk", "walk"}, {"range", 9}};
    EXPECT_THROW(parseAnimations(document), std::runtime_error);

    document["movement"] = {{"style", "float"}, {"walk", "walk"}, {"range", 61}};
    try {
        parseAnimations(document);
        FAIL() << "a radius above the limit must be refused";
    } catch (lens::core::pet::Refused const& refused) {
        EXPECT_EQ(refused.reason(), lens::core::pet::Refusal::TooLarge);
    }
}

TEST(PetConfig, StretchMayBeLeftOut)
{
    // PHASE3 3.1: stretch is not required in the first version, so a document without it still loads.
    auto doc = animationsDocument();
    doc["actions"].erase("stretch");
    doc["roles"].erase("stretch");
    doc["idlePool"]     = json::array({"look_around"});
    auto const playbook = parseAnimations(doc).playbook;
    EXPECT_EQ(playbook.roles.count(Role::Stretch), 0u);
}

TEST(PetConfig, RefusesADocumentMissingARequiredRole)
{
    auto doc = animationsDocument();
    doc["roles"].erase("sleep");
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesADocumentWithoutTheRolesObject)
{
    auto doc = animationsDocument();
    doc.erase("roles");
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesADocumentWithoutTheIdlePool)
{
    auto doc = animationsDocument();
    doc.erase("idlePool");
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesADocumentWithoutTheBlinkEntry)
{
    auto doc = animationsDocument();
    doc["actions"].erase("blink");
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesAFrameCountBelowOne)
{
    auto doc                         = animationsDocument();
    doc["actions"]["idle"]["frames"] = 0;
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesANonPositiveFrameRate)
{
    auto doc                       = animationsDocument();
    doc["actions"]["study"]["fps"] = 0;
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, AcceptsAnActionNameTheCodeHasNeverHeardOf)
{
    // The names are data: a pet may define any action, and the product does not need to know it.
    auto doc                = animationsDocument();
    doc["actions"]["dance"] = {{"frames", 4}, {"fps", 4}, {"loop", false}};
    auto const playbook     = parseAnimations(doc).playbook;
    EXPECT_EQ(playbook.actions.count("dance"), 1u);
}

TEST(PetConfig, RefusesARoleNameTheStateMachineDoesNotKnow)
{
    auto doc              = animationsDocument();
    doc["roles"]["dance"] = "study";
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesARoleMappedToAnActionWithNoPlayback)
{
    auto doc              = animationsDocument();
    doc["roles"]["study"] = "reading";
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesAReturnToARoleThatIsNotInTheDocument)
{
    auto doc                           = animationsDocument();
    doc["actions"]["yawn"]["returnTo"] = "dance";
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesAReturnToARoleTheDocumentDoesNotMap)
{
    // Yawn returns to stretch, but the document maps no role to stretch: the pet would have nothing to play.
    auto doc                           = animationsDocument();
    doc["actions"]["yawn"]["returnTo"] = "stretch";
    doc["roles"].erase("stretch");
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesAnIdlePoolActionWithNoPlayback)
{
    auto doc        = animationsDocument();
    doc["idlePool"] = json::array({"dance"});
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesAnIdlePoolActionThatLoops)
{
    // The idle timer starts a pool action and waits for it to end; a looping one would never end.
    auto doc        = animationsDocument();
    doc["idlePool"] = json::array({"idle"});
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesAMissingActionsObject)
{
    EXPECT_THROW(parseAnimations(json::object()), std::runtime_error);
}

TEST(PetConfig, ParsesAnchorsForEveryFrame)
{
    auto const playbook               = dogPlaybook();
    auto       doc                    = anchorsDocument(animationsDocument());
    doc["actions"]["idle"][2]["head"] = {0, -3};
    auto const anchors                = parseAnchors(doc, playbook);
    ASSERT_EQ(anchors.at("idle").size(), 5u);
    EXPECT_EQ(anchors.at("idle")[2].head.y, -3);
}

TEST(PetConfig, RefusesAnActionWithoutAnchors)
{
    auto const playbook = dogPlaybook();
    auto       doc      = anchorsDocument(animationsDocument());
    doc["actions"].erase("idle");
    EXPECT_THROW(parseAnchors(doc, playbook), std::runtime_error);
}

TEST(PetConfig, RefusesAnchorsThatDoNotMatchTheFrameCount)
{
    auto const playbook = dogPlaybook();
    auto       doc      = anchorsDocument(animationsDocument());
    doc["actions"]["idle"].push_back({{"head", {0, 0}}, {"face", {0, 0}}, {"body", {0, 0}}});
    EXPECT_THROW(parseAnchors(doc, playbook), std::runtime_error);
}

TEST(PetConfig, RefusesAnAnchorPointThatIsMissing)
{
    auto const playbook = dogPlaybook();
    auto       doc      = anchorsDocument(animationsDocument());
    doc["actions"]["study"][3].erase("face");
    EXPECT_THROW(parseAnchors(doc, playbook), std::runtime_error);
}

TEST(PetConfig, RefusesAnchorsForAnActionThePlaybookDoesNotDefine)
{
    auto const playbook     = dogPlaybook();
    auto       doc          = anchorsDocument(animationsDocument());
    doc["actions"]["dance"] = json::array({{{"head", {0, 0}}, {"face", {0, 0}}, {"body", {0, 0}}}});
    EXPECT_THROW(parseAnchors(doc, playbook), std::runtime_error);
}

TEST(PetConfig, ParsesTheAccessoryCatalogue)
{
    auto const catalogue = parseAccessories(accessoriesDocument(), dogPlaybook());
    ASSERT_EQ(catalogue.size(), 7u);
    EXPECT_EQ(catalogue.front().id, "hat");
    EXPECT_EQ(catalogue.back().id, "bowtie");
}

TEST(PetConfig, RefusesAnAccessoryInAnUnknownSlot)
{
    auto doc                      = accessoriesDocument();
    doc["accessories"][0]["slot"] = "hand";
    EXPECT_THROW(parseAccessories(doc, dogPlaybook()), std::runtime_error);
}

TEST(PetConfig, RefusesARepeatedAccessoryId)
{
    auto doc                    = accessoriesDocument();
    doc["accessories"][1]["id"] = "hat";
    EXPECT_THROW(parseAccessories(doc, dogPlaybook()), std::runtime_error);
}

TEST(PetConfig, RefusesAccessoryVisibleInAnActionThePetDoesNotHave)
{
    auto doc                                     = accessoriesDocument();
    doc["accessories"][0]["supportedActions"][0] = "dance";
    EXPECT_THROW(parseAccessories(doc, dogPlaybook()), std::runtime_error);
}

TEST(PetConfig, AnAccessoryMayBeVisibleInAnyActionThePetHas)
{
    auto animations                = animationsDocument();
    animations["actions"]["dance"] = {{"frames", 4}, {"fps", 4}, {"loop", false}};
    auto doc                       = accessoriesDocument();
    doc["accessories"][0]["supportedActions"].push_back("dance");
    auto const catalogue = parseAccessories(doc, parseAnimations(animations).playbook);
    EXPECT_EQ(catalogue.front().supportedActions.back(), "dance");
}

TEST(PetConfig, TheAngryRoleIsOptional)
{
    // A package written before the angry action existed must keep loading: the role is not required.
    auto document = animationsDocument();
    EXPECT_NO_THROW(parseAnimations(document));

    document["actions"]["angry"] = {{"frames", 5}, {"fps", 10}, {"loop", false}};
    document["roles"]["angry"]   = "angry";
    auto const playbook          = parseAnimations(document).playbook;
    EXPECT_EQ(playbook.roles.at(Role::Angry), "angry");
}

TEST(PetConfig, TheReviewSignRoleIsOptional)
{
    // A third-party package without the ask action must keep loading; its review sign then shows with no pose.
    auto document = animationsDocument();
    EXPECT_NO_THROW(parseAnimations(document));

    document["actions"]["ask"] = {{"frames", 4}, {"fps", 4}, {"loop", true}};
    document["roles"]["ask"]   = "ask";
    auto const playbook        = parseAnimations(document).playbook;
    EXPECT_EQ(playbook.roles.at(Role::Ask), "ask");
}

TEST(PetConfig, TheDizzyRoleIsOptional)
{
    // A package written before the landing reaction existed keeps loading; its throws land on click_react instead.
    auto document = animationsDocument();
    EXPECT_NO_THROW(parseAnimations(document));

    document["actions"]["dizzy"] = {{"frames", 6}, {"fps", 6}, {"loop", false}};
    document["roles"]["dizzy"]   = "dizzy";
    auto const playbook          = parseAnimations(document).playbook;
    EXPECT_EQ(playbook.roles.at(Role::Dizzy), "dizzy");
}

TEST(PetConfig, ADurationMarksHowLongALoopingActionRuns)
{
    using namespace std::chrono_literals;
    auto document                = animationsDocument();
    document["actions"]["swing"] = {{"frames", 8}, {"fps", 8}, {"loop", true}, {"duration", 24000}};
    auto const playbook          = parseAnimations(document).playbook;
    EXPECT_EQ(playbook.actions.at("swing").duration, std::optional{24s});
}

TEST(PetConfig, RefusesADurationThatIsNotPositive)
{
    auto document                = animationsDocument();
    document["actions"]["swing"] = {{"frames", 8}, {"fps", 8}, {"loop", true}, {"duration", 0}};
    EXPECT_THROW(parseAnimations(document), std::runtime_error);
}

TEST(PetConfig, ASceneStaysOutOfTheIdlePool)
{
    // It loops, and only the scene roll may start it: the pool holds one-shots only (ADR 0048, 0049).
    auto document                = animationsDocument();
    document["actions"]["swing"] = {{"frames", 8}, {"fps", 8}, {"loop", true}, {"duration", 24000}};
    auto const playbook          = parseAnimations(document).playbook;
    EXPECT_EQ(std::find(playbook.idlePool.begin(), playbook.idlePool.end(), "swing"), playbook.idlePool.end());
}

TEST(PetConfig, BehaviorIsOptionalAndDefaults)
{
    // A package written before the behavior block existed keeps today's temperament (ADR 0094).
    using namespace std::chrono_literals;
    auto const animations = parseAnimations(animationsDocument());
    EXPECT_EQ(animations.behavior.idleBase, 20s);
    EXPECT_EQ(animations.behavior.idleSpread, 70s);
    EXPECT_DOUBLE_EQ(animations.behavior.sceneChance, lens::core::pet::kSceneChance);
    EXPECT_DOUBLE_EQ(animations.behavior.napChance, lens::core::pet::kNapChance);
    EXPECT_EQ(animations.behavior.angryClicks, 3);
    EXPECT_EQ(animations.behavior.angryWindow, 2s);
    EXPECT_EQ(animations.behavior.wanderWaitMin, 120s);
    EXPECT_EQ(animations.behavior.wanderWaitMax, 300s);
    EXPECT_EQ(animations.behavior.strollSteps, 2);
    EXPECT_DOUBLE_EQ(animations.behavior.hideChance, 0.7);
    EXPECT_DOUBLE_EQ(animations.behavior.gazeRadius, lens::core::pet::HoverSense::kGazeRadius);
}

TEST(PetConfig, ParsesTheBehaviorBlock)
{
    using namespace std::chrono_literals;
    auto document        = animationsDocument();
    document["behavior"] = {
        {"idleBaseMs", 12000},
        {"idleSpreadMs", 45000},
        {"sceneChance", 0.2},
        {"napChance", 0.05},
        {"angryClicks", 4},
        {"angryWindowMs", 2500},
        {"wanderWaitMinMs", 90000},
        {"wanderWaitMaxMs", 210000},
        {"strollSteps", 3},
        {"hideChance", 0.5},
        {"gazeRadius", 30.0},
    };
    auto const behavior = parseAnimations(document).behavior;
    EXPECT_EQ(behavior.idleBase, 12s);
    EXPECT_EQ(behavior.idleSpread, 45s);
    EXPECT_DOUBLE_EQ(behavior.sceneChance, 0.2);
    EXPECT_DOUBLE_EQ(behavior.napChance, 0.05);
    EXPECT_EQ(behavior.angryClicks, 4);
    EXPECT_EQ(behavior.angryWindow, 2500ms);
    EXPECT_EQ(behavior.wanderWaitMin, 90s);
    EXPECT_EQ(behavior.wanderWaitMax, 210s);
    EXPECT_EQ(behavior.strollSteps, 3);
    EXPECT_DOUBLE_EQ(behavior.hideChance, 0.5);
    EXPECT_DOUBLE_EQ(behavior.gazeRadius, 30.0);
}

TEST(PetConfig, BehaviorOmittedFieldsKeepTheDefaults)
{
    // A block tuning one knob does not have to repeat the rest.
    using namespace std::chrono_literals;
    auto document        = animationsDocument();
    document["behavior"] = {{"napChance", 0.2}};
    auto const behavior  = parseAnimations(document).behavior;
    EXPECT_DOUBLE_EQ(behavior.napChance, 0.2);
    EXPECT_EQ(behavior.idleBase, 20s);
    EXPECT_EQ(behavior.strollSteps, 2);
    EXPECT_DOUBLE_EQ(behavior.gazeRadius, lens::core::pet::HoverSense::kGazeRadius);
}

TEST(PetConfig, BehaviorStaysWithinItsBounds)
{
    auto       document = animationsDocument();
    auto const refuse   = [&document] { return parseAnimations(document).behavior; };

    document["behavior"] = {{"idleBaseMs", 999}};
    EXPECT_THROW(refuse(), std::runtime_error);
    document["behavior"] = {{"idleBaseMs", 600001}};
    try {
        refuse();
        FAIL() << "an idle base above the limit must be refused";
    } catch (lens::core::pet::Refused const& refused) {
        EXPECT_EQ(refused.reason(), lens::core::pet::Refusal::TooLarge);
    }
    document["behavior"] = {{"idleSpreadMs", -1}};
    EXPECT_THROW(refuse(), std::runtime_error);
    document["behavior"] = {{"sceneChance", 1.5}};
    EXPECT_THROW(refuse(), std::runtime_error);
    document["behavior"] = {{"napChance", -0.1}};
    EXPECT_THROW(refuse(), std::runtime_error);
    document["behavior"] = {{"angryClicks", 0}};
    EXPECT_THROW(refuse(), std::runtime_error);
    document["behavior"] = {{"angryClicks", 11}};
    EXPECT_THROW(refuse(), std::runtime_error);
    document["behavior"] = {{"strollSteps", 5}};
    EXPECT_THROW(refuse(), std::runtime_error);
    document["behavior"] = {{"hideChance", 1.01}};
    EXPECT_THROW(refuse(), std::runtime_error);
    document["behavior"] = {{"gazeRadius", 20.0}};
    // The eyes' reach must stay beyond the near ring and its exit margin.
    EXPECT_THROW(refuse(), std::runtime_error);
    document["behavior"] = {{"gazeRadius", 129.0}};
    EXPECT_THROW(refuse(), std::runtime_error);
    document["behavior"] = {{"wanderWaitMinMs", 300000}, {"wanderWaitMaxMs", 120000}};
    EXPECT_THROW(refuse(), std::runtime_error);
    document["behavior"] = {{"sceneChance", std::numeric_limits<double>::infinity()}};
    EXPECT_THROW(refuse(), std::runtime_error);

    // The edges themselves are legal.
    document["behavior"] = {{"idleBaseMs", 1000}, {"idleSpreadMs", 0}, {"sceneChance", 0.0}, {"napChance", 1.0}, {"strollSteps", 1}, {"gazeRadius", 20.5}};
    auto const behavior  = refuse();
    EXPECT_EQ(behavior.idleBase, std::chrono::milliseconds{1000});
    EXPECT_DOUBLE_EQ(behavior.sceneChance, 0.0);
    EXPECT_EQ(behavior.strollSteps, 1);
    EXPECT_DOUBLE_EQ(behavior.gazeRadius, 20.5);
}

} // namespace
