#include "SystemCommands.h"

#include <iostream>
#include <optional>
#include <string>

namespace {

int g_failures = 0;

void ExpectTrue(bool condition, const char* expression, const char* file, int line) {
    if (condition) {
        return;
    }
    ++g_failures;
    std::wcerr << L"FAIL " << file << L":" << line << L"  EXPECT_TRUE(" << expression << L")\n";
}

#define EXPECT_TRUE(condition) ExpectTrue((condition), #condition, __FILE__, __LINE__)

void ExpectCommand(
    const std::wstring& query,
    nexus::SystemCommandKind kind,
    const std::wstring& label) {
    const auto command = nexus::TryParseSystemCommand(query);
    if (!command) {
        ++g_failures;
        std::wcerr << L"FAIL expected command for \"" << query << L"\"\n";
        return;
    }
    if (command->kind != kind) {
        ++g_failures;
        std::wcerr << L"FAIL kind mismatch for \"" << query << L"\"\n";
    }
    if (command->label != label) {
        ++g_failures;
        std::wcerr << L"FAIL label mismatch for \"" << query << L"\" got \"" << command->label
                   << L"\"\n";
    }
}

void ExpectVolume(const std::wstring& query, int percent) {
    const auto command = nexus::TryParseSystemCommand(query);
    if (!command || command->kind != nexus::SystemCommandKind::SetVolume) {
        ++g_failures;
        std::wcerr << L"FAIL expected volume command for \"" << query << L"\"\n";
        return;
    }
    if (command->volumePercent != percent) {
        ++g_failures;
        std::wcerr << L"FAIL volume percent for \"" << query << L"\" expected " << percent
                   << L" got " << command->volumePercent << L"\n";
    }
}

void ExpectKill(const std::wstring& query, const std::wstring& processName) {
    const auto command = nexus::TryParseSystemCommand(query);
    if (!command || command->kind != nexus::SystemCommandKind::KillProcess) {
        ++g_failures;
        std::wcerr << L"FAIL expected kill command for \"" << query << L"\"\n";
        return;
    }
    if (command->processName != processName) {
        ++g_failures;
        std::wcerr << L"FAIL process name for \"" << query << L"\" expected \"" << processName
                   << L"\" got \"" << command->processName << L"\"\n";
    }
}

void ExpectNoCommand(const std::wstring& query) {
    if (nexus::TryParseSystemCommand(query).has_value()) {
        ++g_failures;
        std::wcerr << L"FAIL expected no system command for \"" << query << L"\"\n";
    }
}

void TestSimpleCommands() {
    ExpectCommand(L"lock", nexus::SystemCommandKind::Lock, L"Bloquear sesi\u00F3n");
    ExpectCommand(L" LOCK ", nexus::SystemCommandKind::Lock, L"Bloquear sesi\u00F3n");
    ExpectCommand(L"sleep", nexus::SystemCommandKind::Sleep, L"Suspender equipo");
    ExpectCommand(L"restart", nexus::SystemCommandKind::Restart, L"Reiniciar equipo");
    ExpectCommand(L"shutdown", nexus::SystemCommandKind::Shutdown, L"Apagar equipo");
    ExpectCommand(L"mute", nexus::SystemCommandKind::Mute, L"Silenciar volumen");
    ExpectCommand(
        L"empty trash",
        nexus::SystemCommandKind::EmptyTrash,
        L"Vaciar papelera de reciclaje");
    ExpectCommand(
        L" Empty Trash ",
        nexus::SystemCommandKind::EmptyTrash,
        L"Vaciar papelera de reciclaje");
    ExpectCommand(
        L"empty  trash",
        nexus::SystemCommandKind::EmptyTrash,
        L"Vaciar papelera de reciclaje");
}

void TestVolumeCommands() {
    ExpectVolume(L"vol 0", 0);
    ExpectVolume(L"vol 50", 50);
    ExpectVolume(L"VOL 100", 100);
    ExpectVolume(L" vol\t75 ", 75);
}

void TestKillCommands() {
    ExpectKill(L"kill notepad", L"notepad");
    ExpectKill(L"KILL chrome.exe", L"chrome.exe");
    ExpectKill(L"kill   Discord", L"Discord");
}

void TestRejections() {
    ExpectNoCommand(L"");
    ExpectNoCommand(L"   ");
    ExpectNoCommand(L"locker");
    ExpectNoCommand(L"lock now");
    ExpectNoCommand(L"sleeping");
    ExpectNoCommand(L"empty");
    ExpectNoCommand(L"trash");
    ExpectNoCommand(L"empty bin");
    ExpectNoCommand(L"kill");
    ExpectNoCommand(L"kill ");
    ExpectNoCommand(L"killme");
    ExpectNoCommand(L"vol");
    ExpectNoCommand(L"vol ");
    ExpectNoCommand(L"vol abc");
    ExpectNoCommand(L"vol 101");
    ExpectNoCommand(L"vol -1");
    ExpectNoCommand(L"vol 50 extra");
    ExpectNoCommand(L"g lock");
    ExpectNoCommand(L"-p 3000");
}

void TestFindProcessesByName() {
    // Empty filter never matches.
    EXPECT_TRUE(nexus::FindProcessesByName(L"").empty());

    // Matching the current image name should return at least one process on a normal system,
    // but never include PID 0 / 4. Use a very unlikely name for the empty case.
    EXPECT_TRUE(nexus::FindProcessesByName(L"zzznexus-no-such-process-xyz").empty());
}

}  // namespace

int RunSystemCommandsTests() {
    g_failures = 0;
    TestSimpleCommands();
    TestVolumeCommands();
    TestKillCommands();
    TestRejections();
    TestFindProcessesByName();
    return g_failures;
}
