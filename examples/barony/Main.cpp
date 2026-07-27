// KoreLibrary example payload.
//
// Builds to a DLL you inject into a running game. INSERT opens the menu, END
// unloads the library cleanly.
//
// The features below are game-agnostic on purpose: a watermark, a live memory
// inspector, and a signature scanner. Those last two are the tools you actually
// use to find a specific game's structures — start there, then write real
// features against what you find. A sketch of what a game-specific feature
// looks like is at the bottom.

#include <Kore/Kore.hpp>

#include <array>
#include <cstdlib>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <vector>

using namespace Kore;

// ---------------------------------------------------------------- watermark

class Watermark : public Feature {
public:
    const char* Name() const override { return "Watermark"; }
    const char* Category() const override { return "Visuals"; }
    const char* Description() const override {
        return "Small always-on overlay showing frame rate and the attached process.";
    }
    bool Toggleable() const override { return false; }

    void OnAttach() override {
        m_process = Memory::Module::Main().Name();
    }

    void OnRender() override {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 12.0f, viewport->WorkPos.y + 12.0f));
        ImGui::SetNextWindowBgAlpha(0.55f);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

        if (ImGui::Begin("##kore_watermark", nullptr, flags)) {
            ImGui::Text("KoreLibrary | %s | %.0f fps", m_process.c_str(),
                        static_cast<double>(ImGui::GetIO().Framerate));
        }
        ImGui::End();
    }

private:
    std::string m_process;
};

// ---------------------------------------------------------- memory inspector

class MemoryInspector final : public Feature {
public:
    const char* Name() const override { return "Memory inspector"; }
    const char* Category() const override { return "Tools"; }
    const char* Description() const override {
        return "Read a live address as several types at once. Supports a base module "
               "plus offset, so results survive ASLR between runs.";
    }

    void OnMenu() override {
        if (!Enabled())
            return;

        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputText("Module", m_module, sizeof(m_module));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputText("Offset (hex)", m_offset, sizeof(m_offset),
                         ImGuiInputTextFlags_CharsHexadecimal);

        const Address address = Resolve();
        if (address == 0) {
            ImGui::TextDisabled("Module not loaded, or offset is empty.");
            return;
        }

        ImGui::Text("Address: %p", reinterpret_cast<void*>(address));

        if (!Memory::IsReadable(address, sizeof(double))) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Not readable.");
            return;
        }

        ImGui::Separator();
        ImGui::Text("i32    %d", Memory::Read<i32>(address));
        ImGui::Text("i64    %lld", static_cast<long long>(Memory::Read<i64>(address)));
        ImGui::Text("float  %.4f", static_cast<double>(Memory::Read<float>(address)));
        ImGui::Text("double %.4f", Memory::Read<double>(address));
        ImGui::Text("ptr    %p", reinterpret_cast<void*>(Memory::Read<Address>(address)));

        // A readable pointer here usually means you've found a struct field
        // worth following — the next step in building a pointer chain.
        if (const Address deref = Memory::Read<Address>(address);
            Memory::IsReadable(deref, 8)) {
            ImGui::TextDisabled("  -> dereferences to readable memory");
        }
    }

private:
    Address Resolve() const {
        if (m_offset[0] == '\0')
            return 0;

        const auto module = (m_module[0] == '\0')
                                ? std::optional<Memory::Module>(Memory::Module::Main())
                                : Memory::Module::Find(m_module);
        if (!module)
            return 0;

        return module->Base() + std::strtoull(m_offset, nullptr, 16);
    }

    char m_module[64]{};
    char m_offset[32]{};
};

// ----------------------------------------------------------- pattern scanner

class PatternScanner final : public Feature {
public:
    const char* Name() const override { return "Signature scanner"; }
    const char* Category() const override { return "Tools"; }
    const char* Description() const override {
        return "Scan a module's .text for an IDA-style signature. A good signature "
               "returns exactly one hit — more than one means widen it.";
    }

    void OnMenu() override {
        if (!Enabled())
            return;

        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputText("Module##scan", m_module, sizeof(m_module));

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##sig", "48 8B 05 ? ? ? ? 48 85 C0", m_signature, sizeof(m_signature));

        if (ImGui::Button("Scan"))
            Scan();

        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_status.c_str());

        if (m_hits.empty())
            return;

        ImGui::Separator();
        if (ImGui::BeginChild("##hits", ImVec2(0.0f, 120.0f))) {
            const auto module = ResolveModule();
            const Address base = module ? module->Base() : 0;

            for (const Address hit : m_hits) {
                ImGui::Text("%p", reinterpret_cast<void*>(hit));
                if (base) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("  base+%llX", static_cast<unsigned long long>(hit - base));
                }
            }
        }
        ImGui::EndChild();
    }

private:
    std::optional<Memory::Module> ResolveModule() const {
        if (m_module[0] == '\0')
            return Memory::Module::Main();
        return Memory::Module::Find(m_module);
    }

    void Scan() {
        m_hits.clear();

        const auto module = ResolveModule();
        if (!module) {
            m_status = "module not found";
            return;
        }

        const Memory::Pattern pattern(m_signature);
        if (!pattern.Valid()) {
            m_status = "malformed signature";
            return;
        }

        m_hits = pattern.ScanAll(module->Text());

        if (m_hits.size() > 32) {
            m_hits.resize(32);
            m_status = "32+ hits (too generic)";
        } else {
            m_status = std::format("{} hit(s)", m_hits.size());
        }

        KORE_INFO("Scan of {} for '{}': {}", module->Name(), m_signature, m_status);
    }

    char                 m_module[64]{};
    char                 m_signature[256]{};
    std::string          m_status = "idle";
    std::vector<Address> m_hits;
};

// --------------------------------------------------------- module list

class ModuleList final : public Feature {
public:
    const char* Name() const override { return "Loaded modules"; }
    const char* Category() const override { return "Tools"; }
    const char* Description() const override {
        return "Which DLLs the game has loaded, and where. Tells you what to scan "
               "and which graphics API is in play.";
    }

    void OnMenu() override {
        if (!Enabled())
            return;

        ImGui::Text("Overlay backend: %s",
                    Render::Overlay::Get().Kind() == Render::Backend::OpenGL ? "OpenGL" :
                    Render::Overlay::Get().Kind() == Render::Backend::D3D11  ? "D3D11" : "none");

        const auto main = Memory::Module::Main();
        ImGui::Text("%-24s %p (%zu KiB)", main.Name().c_str(),
                    reinterpret_cast<void*>(main.Base()), main.Size() / 1024);

        ImGui::Separator();
        for (const char* name : { "SDL2.dll", "opengl32.dll", "d3d11.dll", "dxgi.dll", "d3d9.dll" }) {
            if (const auto module = Memory::Module::Find(name)) {
                ImGui::Text("%-24s %p", name, reinterpret_cast<void*>(module->Base()));
            } else {
                ImGui::TextDisabled("%-24s not loaded", name);
            }
        }
    }
};

// ------------------------------------------------------------- draw sandbox

/// Renders every drawing primitive at a known position. Its real job is
/// answering "is the overlay actually compositing correctly?" before you go
/// hunting for entity data — if these shapes look right, your backend is fine
/// and any ESP problem is in your memory reads or your matrix.
class DrawSandbox final : public Feature {
public:
    const char* Name() const override { return "Draw sandbox"; }
    const char* Category() const override { return "Visuals"; }
    const char* Description() const override {
        return "Draws every ESP primitive so you can confirm the overlay renders "
               "before debugging game-specific code.";
    }

    void OnRender() override {
        using namespace Render;

        if (!Draw::Ready())
            return;

        const Vec2 screen = Draw::ScreenSize();
        const Vec2 origin{ screen.x * 0.5f - 150.0f, screen.y * 0.5f - 120.0f };

        const Vec2 boxMin{ origin.x, origin.y };
        const Vec2 boxMax{ origin.x + 90.0f, origin.y + 200.0f };

        Draw::CornerBox(boxMin, boxMax, Color::Green(), 1.5f);
        Draw::HealthBar(boxMin, boxMax, m_health);
        Draw::TextCentered({ (boxMin.x + boxMax.x) * 0.5f, boxMin.y - 16.0f },
                           "CornerBox", Color::White(), TextStyle::Outlined);

        const Vec2 box2Min{ origin.x + 160.0f, origin.y };
        const Vec2 box2Max{ origin.x + 250.0f, origin.y + 200.0f };
        Draw::OutlinedBox(box2Min, box2Max, Color::Cyan(), 1.0f);
        Draw::TextCentered({ (box2Min.x + box2Max.x) * 0.5f, box2Min.y - 16.0f },
                           "OutlinedBox", Color::White(), TextStyle::Outlined);

        Draw::Circle({ origin.x + 320.0f, origin.y + 60.0f }, 40.0f, Color::Orange(), 2.0f);
        Draw::CircleFilled({ origin.x + 320.0f, origin.y + 160.0f }, 24.0f,
                           Color::Blue().WithAlpha(140));

        Draw::ProgressBar({ origin.x, boxMax.y + 24.0f }, { origin.x + 250.0f, boxMax.y + 36.0f },
                          m_health, Color::Yellow());

        if (m_tracer)
            Draw::Tracer({ boxMin.x + 45.0f, boxMax.y }, Color::Red().WithAlpha(180), 1.5f);

        // Animate the bars so a frozen overlay is immediately obvious.
        m_health += m_direction * ImGui::GetIO().DeltaTime * 0.35f;
        if (m_health <= 0.0f) { m_health = 0.0f; m_direction = 1.0f; }
        if (m_health >= 1.0f) { m_health = 1.0f; m_direction = -1.0f; }
    }

    void OnMenu() override {
        if (!Enabled())
            return;
        ImGui::Checkbox("Show tracer", &m_tracer);
        ImGui::SliderFloat("Bar value", &m_health, 0.0f, 1.0f);
    }

private:
    float m_health    = 1.0f;
    float m_direction = -1.0f;
    bool  m_tracer    = true;
};

// ------------------------------------------------- game-specific feature shape
//
// This is the pattern every real feature follows. It is left disabled because
// the signature below is a placeholder — you have to find the real one for your
// build of the game in a disassembler. The structure is the point:
//
//   1. Find the code with a signature, never a hardcoded address.
//   2. Build a Memory::Patch so OnDisable can put the original bytes back.
//   3. Report clearly when the signature misses, instead of patching whatever
//      happens to be at address zero.

#if 0
class NoDamage final : public Feature {
public:
    const char* Name() const override { return "No damage"; }
    const char* Category() const override { return "Player"; }

    void OnAttach() override {
        // Placeholder — replace with a signature for the instruction that
        // writes the damage result back to the player's health field.
        const Memory::Pattern pattern("89 41 ?? 8B 45 ?? 85 C0");

        const Address site = pattern.Scan(Memory::Module::Main());
        if (!site) {
            KORE_WARN("'{}': signature not found; feature disabled", Name());
            return;
        }

        KORE_INFO("'{}': found write site at {:#x}", Name(), site);

        // Three NOPs over the store: the damage is computed but never applied.
        m_patch = Memory::Patch(site, std::array<u8, 3>{ 0x90, 0x90, 0x90 });
    }

    void OnEnable() override {
        if (m_patch.Valid())
            m_patch.Apply();
    }

    void OnDisable() override {
        if (m_patch.Applied())
            m_patch.Revert();
    }

private:
    Memory::Patch m_patch;
};
#endif

// ------------------------------------------------------------------- entry

KORE_ENTRY(options) {
    options.name    = "BaronyMenu";
    options.console = true;          // turn off once you're past bring-up
    options.menuKey = VK_INSERT;
    options.unloadKey = VK_END;

    // Barony renders through SDL2. Waiting for it avoids racing an injector
    // that fires before the window exists.
    options.waitForModule = "SDL2.dll";
    options.waitTimeoutMs = 30000;

    options.onAttach = [] {
        auto& features = FeatureManager::Get();
        features.Add<Watermark>();
        features.Add<DrawSandbox>();
        features.Add<ModuleList>();
        features.Add<MemoryInspector>();
        features.Add<PatternScanner>();
        // features.Add<NoDamage>();

        Menu::Get().SetSubtitle("INSERT toggles · END unloads");
    };

    options.onDetach = [] {
        KORE_INFO("Payload detaching");
    };
}
