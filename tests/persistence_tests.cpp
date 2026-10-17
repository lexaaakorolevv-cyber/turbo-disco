#include "test_world.hpp"
#include "storage.hpp"
#include <iostream>
using namespace v2;
int main() {
    try {
        auto run = recorded();
        auto json = encode(run);
        check(encode(decode(json)) == json, "Macro JSON changed on roundtrip");
        json["verified"] = true;
        auto decoded = decode(json);
        check(!decoded.verified, "Disk flag bypassed runtime verification");
        auto bad = json;
        bad["actions"][0][3] = 3;
        bool rejected = false;
        try {
            decode(bad);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Invalid player accepted");
        bad = json;
        bad["trace"].erase(bad["trace"].begin() + 1);
        rejected = false;
        try {
            decode(bad);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Missing mandatory trace accepted");
        Session session{run, Search{}, Candidate{}, Brain{}.learning(), 1, 3};
        auto restored = decodeSession(encode(session));
        check(encode(restored) == encode(session), "Session roundtrip changed state");
        auto dir = std::filesystem::temp_directory_path() / "routefinder-v2-storage-tests";
        std::filesystem::remove_all(dir);
        {
            Storage storage;
            for (unsigned i = 0; i < 8; i++) {
                auto s = session;
                s.attempts = i + 1;
                storage.submit(dir, std::move(s));
            }
            storage.drain();
            check(storage.error().empty(), "Background write failed");
            check(decodeSession(readJson(dir / "search.json")).attempts == 8, "Latest queued state lost");
            check(!std::filesystem::exists(dir / "macro.json"), "Unverified macro published as successful");
        }
        auto path = dir / "best.json";
        std::filesystem::rename(path, path.string() + ".bak");
        check(decode(readJson(path)).actions == run.actions, "Backup recovery failed");
        std::filesystem::remove_all(dir);
        std::cout << "Macro/session JSON validation, untrusted completion flag, background coalescing and "
                     "atomic backup recovery PASS\n";
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
