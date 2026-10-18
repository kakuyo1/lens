/**
 * @file sync_run_test.cpp
 * @brief The sync run's own decisions, offline: which key opens the cloud copy, and whether the
 *        merged document goes back up.
 *
 * These are the steps SyncDuty::process() used to run in one Qt method with a real GistClient
 * behind it, which is why the first-run key, the reopen, the pass that has nothing to send and the
 * failure paths had no case. Every passphrase and every key here is a fixture: nothing reaches
 * GitHub, no event loop runs, and no real key is derived under a real passphrase.
 */

#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/sync_run.h"
#include "core/vault.h"

namespace {

namespace vault   = lens::core::vault;
namespace syncrun = lens::core::syncrun;

using nlohmann::json;
using syncrun::StoredKey;

/// A salt and an iteration count small enough that a case runs in milliseconds. The envelope carries
/// them, so a run that reopens it derives under these and not under the production figure.
vault::KdfParams quickParams()
{
    vault::KdfParams params = vault::newParams();
    params.iterations       = 1000;
    return params;
}

/// @return A sealed cloud copy holding @p document, as the text the gist file carries.
std::string sealedCopy(const StoredKey& key, const json& document)
{
    return vault::seal(key.key, document.dump(), key.params).dump();
}

/// @return The key a passphrase derives under a small iteration count, for fixtures.
StoredKey aKey(const char* passphrase, const vault::KdfParams& params)
{
    return {vault::deriveKey(passphrase, params), params};
}

} // namespace

/// The first run on a device that has no cloud copy yet: the passphrase makes a brand new key, and
/// it is written to the section so the next run needs no passphrase. Nothing has ever been sealed
/// under these parameters before, which is why the run has to mint them itself.
TEST(SyncRun, AFirstRunMintsAKeyAndCreatesTheCloudCopy)
{
    const auto opened = syncrun::openCloudCopy({.passphrase = "a fixture passphrase"});

    ASSERT_TRUE(opened.error.empty()) << opened.error;
    ASSERT_TRUE(opened.keyToStore.has_value()) << "the first key is the one that has to be stored";
    EXPECT_EQ(opened.keyToStore->key.size(), 32u);
    EXPECT_EQ(opened.keyToStore->params.salt.size(), 16u);
    EXPECT_EQ(opened.keyToStore->params.iterations, vault::kIterations);
    EXPECT_EQ(opened.remote, json::object()) << "there was no cloud copy to read";
    EXPECT_TRUE(opened.key.has_value());
}

/// The passphrase is what the reader typed, and a wrong one must not cost them the key they had:
/// nothing is stored, so a second run still opens with the key the first one wrote.
TEST(SyncRun, AWrongPassphraseStoresNothing)
{
    const StoredKey cloud = aKey("the right one", quickParams());
    const json      cloudDocument{{"known", json::object()}};

    const auto opened = syncrun::openCloudCopy({.envelopeText = sealedCopy(cloud, cloudDocument),
                                                .passphrase   = "the wrong one",
                                                .stored       = cloud});

    EXPECT_EQ(opened.error, "passphrase");
    EXPECT_FALSE(opened.keyToStore.has_value()) << "the key that was already there has to survive";
    EXPECT_FALSE(opened.key.has_value());
}

/// An existing cloud copy names the salt and the iteration count it was sealed under. Deriving
/// anything else opens nothing, so a second device has to use the copy's own parameters.
TEST(SyncRun, ASecondDeviceReopensWithTheSaltTheCloudCopyWasSealedUnder)
{
    const StoredKey cloud = aKey("the passphrase", quickParams());
    const json      cloudDocument{{"known", {{"panel", true}}}};

    const auto opened = syncrun::openCloudCopy({.envelopeText = sealedCopy(cloud, cloudDocument),
                                                .passphrase   = "the passphrase"});

    ASSERT_TRUE(opened.error.empty()) << opened.error;
    ASSERT_TRUE(opened.key.has_value());
    EXPECT_EQ(opened.key->params.salt, cloud.params.salt);
    EXPECT_EQ(opened.key->params.iterations, cloud.params.iterations);
    EXPECT_EQ(opened.key->key, cloud.key) << "the same passphrase and salt have to give the same key";
    EXPECT_EQ(opened.remote, cloudDocument);
    // What the run stored is what the cloud copy opens under, so the next run needs no passphrase.
    ASSERT_TRUE(opened.keyToStore.has_value());
    EXPECT_EQ(opened.keyToStore->key, cloud.key);
}

/// The run after the first one: the reader asks for a sync and no passphrase comes with it, so the
/// stored key is what opens the cloud copy.
TEST(SyncRun, TheStoredKeyReopensTheCloudCopyWithNoPassphrase)
{
    const StoredKey cloud = aKey("the passphrase", quickParams());
    const json      cloudDocument{{"history", json::array()}};

    const auto opened = syncrun::openCloudCopy({.envelopeText = sealedCopy(cloud, cloudDocument),
                                                .stored       = cloud});

    ASSERT_TRUE(opened.error.empty()) << opened.error;
    EXPECT_EQ(opened.remote, cloudDocument);
    EXPECT_FALSE(opened.keyToStore.has_value()) << "the run stored nothing new, so it writes nothing";
}

/// A stored key is a key this device wrote once; a cloud copy that will not open under it means the
/// passphrase behind it was mistyped, or the section was edited, and neither is worth retrying.
TEST(SyncRun, AStoredKeyThatCannotOpenTheCloudCopyAsksForThePassphrase)
{
    const StoredKey stored = aKey("one passphrase", quickParams());
    const StoredKey cloud  = aKey("another passphrase", quickParams());

    const auto opened = syncrun::openCloudCopy({.envelopeText = sealedCopy(cloud, json::object()),
                                                .stored       = stored});

    EXPECT_EQ(opened.error, "passphrase");
}

/// Nothing on the device and no passphrase with the run: the reader has to supply one, and the
/// error is the one that asks them to rather than a failure the page would report as the network's.
TEST(SyncRun, NoStoredKeyAndNoPassphraseAsksForThePassphrase)
{
    const auto opened = syncrun::openCloudCopy({});

    EXPECT_EQ(opened.error, "passphrase");
    EXPECT_FALSE(opened.key.has_value());
}

/// A gist file that is not a JSON object is not a cloud copy. Whatever the key situation is, the
/// reader is told the sync failed: there is nothing to merge and nothing to ask the passphrase for.
TEST(SyncRun, ACloudCopyThatIsNotADocumentFailsTheRun)
{
    const StoredKey stored = aKey("the passphrase", quickParams());

    EXPECT_EQ(syncrun::openCloudCopy({.envelopeText = "[]", .stored = stored}).error, "generic");
    EXPECT_EQ(syncrun::openCloudCopy({.envelopeText = "{not json", .passphrase = "the passphrase"}).error, "generic");
    // An envelope whose kdf block is not there cannot be derived against either.
    EXPECT_EQ(syncrun::openCloudCopy({.envelopeText = R"({"format":1})", .passphrase = "the passphrase"}).error, "generic");
}

/// The key opened what was there, so it is this device's key whatever the copy holds: a cloud copy
/// that decrypts to something other than a document is a failed sync that kept its key.
TEST(SyncRun, ACloudCopyThatIsNotADocumentStillKeepsTheKeyThatOpenedIt)
{
    const StoredKey cloud = aKey("the passphrase", quickParams());

    const auto opened = syncrun::openCloudCopy({.envelopeText = sealedCopy(cloud, json::array()),
                                                .passphrase   = "the passphrase"});

    EXPECT_EQ(opened.error, "generic");
    ASSERT_TRUE(opened.keyToStore.has_value());
    EXPECT_EQ(opened.keyToStore->key, cloud.key);
}

/// The ten-minute poll. It pulls and merges like any other pass, and on a device where nothing has
/// changed it has nothing to send: no call, no envelope, and the run is over.
TEST(SyncRun, ATimerPassWithNothingNewSendsNothing)
{
    const StoredKey key = aKey("the passphrase", quickParams());
    const json      merged{{"known", {{"panel", true}}}};

    const auto upload = syncrun::sealUpload({.key                 = key,
                                             .merged              = merged,
                                             .gistId              = "gist-1",
                                             .manual              = false,
                                             .fingerprint         = "same",
                                             .uploadedFingerprint = "same"});

    EXPECT_EQ(upload.command, syncrun::Upload::Command::None);
    EXPECT_TRUE(upload.sealed.empty()) << "nothing was sealed, so there is nothing to put in the file";
}

/// The same pull, asked for by the reader: they pressed the button, and the answer they are waiting
/// for is that the cloud copy has been written back.
TEST(SyncRun, ASyncTheReaderAskedForAlwaysUploads)
{
    const StoredKey key = aKey("the passphrase", quickParams());
    const json      merged{{"known", {{"panel", true}}}};

    const auto upload = syncrun::sealUpload({.key                 = key,
                                             .merged              = merged,
                                             .gistId              = "gist-1",
                                             .manual              = true,
                                             .fingerprint         = "same",
                                             .uploadedFingerprint = "same"});

    EXPECT_EQ(upload.command, syncrun::Upload::Command::Update);
    EXPECT_EQ(upload.gistId, "gist-1");
    EXPECT_FALSE(upload.sealed.empty());
}

/// A change made on this device since the last upload is what the fingerprints are for, and a timer
/// pass that found one sends it.
TEST(SyncRun, ATimerPassWithSomethingNewUpdatesTheCopy)
{
    const StoredKey key = aKey("the passphrase", quickParams());
    const json      merged{{"known", {{"panel", true}}}};

    const auto upload = syncrun::sealUpload({.key                 = key,
                                             .merged              = merged,
                                             .gistId              = "gist-1",
                                             .manual              = false,
                                             .fingerprint         = "moved on",
                                             .uploadedFingerprint = "as it was"});

    EXPECT_EQ(upload.command, syncrun::Upload::Command::Update);
}

/// There is no gist to replace on an account that has never synced, so the fingerprints have nothing
/// to compare against and a copy has to be made even when this device has sent nothing.
TEST(SyncRun, AnAccountWithNoSyncGistCreatesOne)
{
    const StoredKey key = aKey("the passphrase", quickParams());
    const json      merged{{"known", json::object()}};

    const auto upload = syncrun::sealUpload({.key                 = key,
                                             .merged              = merged,
                                             .manual              = false,
                                             .fingerprint         = "same",
                                             .uploadedFingerprint = "same"});

    EXPECT_EQ(upload.command, syncrun::Upload::Command::Create);
    EXPECT_TRUE(upload.gistId.empty()) << "there is no gist to name yet";
}

/// What goes up is the merged document, sealed under the key in force and the parameters that key
/// was derived with -- the same three values another device reads out of the envelope.
TEST(SyncRun, TheUploadIsTheMergedDocumentUnderTheKeyInForce)
{
    const StoredKey key = aKey("the passphrase", quickParams());
    const json      merged{{"known", {{"panel", true}}}, {"format", 1}};

    // Asked for, so the fingerprints below do not get to skip the upload this case is about.
    const auto upload = syncrun::sealUpload({.key    = key,
                                             .merged = merged,
                                             .gistId = "gist-1",
                                             .manual = true});

    ASSERT_EQ(upload.command, syncrun::Upload::Command::Update);
    const auto envelope = json::parse(upload.sealed);
    EXPECT_EQ(vault::paramsOf(envelope)->salt, key.params.salt);
    EXPECT_EQ(vault::paramsOf(envelope)->iterations, key.params.iterations);
    const auto plaintext = vault::open(key.key, envelope);
    ASSERT_TRUE(plaintext.has_value());
    EXPECT_EQ(json::parse(*plaintext), merged);
}

/// A 401 is worth one renewal: the refresh token buys a new access token and the run carries on
/// from where the 401 stopped it. A second one, a run with no refresh token stored, and a refusal
/// that was not the token's are all reported to the reader instead.
TEST(SyncRun, ARefusedCallIsRenewedOnceAndOnlyWithATokenStored)
{
    using syncrun::shouldRenewSignIn;

    EXPECT_TRUE(shouldRenewSignIn(true, true, false));
    EXPECT_FALSE(shouldRenewSignIn(true, true, true)) << "one renewal per run";
    EXPECT_FALSE(shouldRenewSignIn(true, false, false)) << "an app whose tokens do not expire sends none";
    EXPECT_FALSE(shouldRenewSignIn(false, true, false)) << "another refusal is not the token's";
}