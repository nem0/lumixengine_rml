#define LUMIX_NO_CUSTOM_CRT
#include "RmlUi/Core.h"
#include "engine/allocator.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/plugin.h"
#include "engine/reflection.h"
#include "engine/string.h"
#include "renderer/pipeline.h"
#include "renderer/render_module.h"
#include "renderer/renderer.h"
#include "rml_module.h"

namespace Lumix {

struct RMLRenderPlugin : RenderPlugin {
	void renderUI(Pipeline& pipeline) override {
		World& world = pipeline.getModule()->getWorld();
		RMLModule* module = static_cast<RMLModule*>(world.getModule("rml"));
		module->render(pipeline);
	}
};

struct SystemInterface : Rml::SystemInterface {
	double GetElapsedTime() override { return m_timer.getTimeSinceStart(); }
	// int TranslateString(String& translated, const String& input);
	// void JoinPath(String& translated_path, const String& document_path, const String& path);
	bool LogMessage(Rml::Log::Type type, const Rml::String& message) {
		if (type == Rml::Log::LT_WARNING)
			logWarning("Rml: ", message.c_str());
		else if (type == Rml::Log::LT_ERROR)
			logError("Rml", message.c_str());
		else
			logInfo("Rml", message.c_str());
		return true;
	}
	// void SetMouseCursor(const String& cursor_name);
	// void SetClipboardText(const String& text);
	// void GetClipboardText(String& text);
	// void ActivateKeyboard();
	// void DeactivateKeyboard();
	os::Timer m_timer;
};

struct RMLSystem : ISystem {
	RMLSystem(Engine& engine)
		: m_engine(engine)
	{
		RMLModule::reflect();
	}

	~RMLSystem() override {
		ISystem* renderer = m_engine.getSystemManager().getSystem("renderer");
		if (renderer) {
			((Renderer*)renderer)->removePlugin(m_render_plugin);
		}
		Rml::Shutdown();
	}

	void systemAdded(ISystem& plugin) override {
		if (equalStrings(plugin.getName(), "renderer")) {
			Renderer& r = (Renderer&)plugin;
			r.addPlugin(m_render_plugin);
		}
	}


	const char* getName() const override { return "rml"; }
	void serialize(OutputMemoryStream& serializer) const override {}
	bool deserialize(i32 version, InputMemoryStream& serializer) override { return version == 0; }

	void initBegin() override {
		Rml::SetSystemInterface(&m_system_interface);
		Rml::Initialise();
		// Rml::LoadFontFace("editor/fonts/NotoSans-Regular.ttf", true);
		Rml::LoadFontFace("rml/Delicious-Bold.otf");
		Rml::LoadFontFace("rml/Delicious-BoldItalic.otf");
		Rml::LoadFontFace("rml/Delicious-Italic.otf");
		Rml::LoadFontFace("rml/Delicious-Roman.otf");
	}

	void createModules(World& world) override { world.addModule(RMLModule::create(*this, m_engine, world)); }

	Engine& m_engine;
	SystemInterface m_system_interface;
	RMLRenderPlugin m_render_plugin;
};

LUMIX_PLUGIN_ENTRY(rml) {
	return LUMIX_NEW(engine.getAllocator(), RMLSystem)(engine);
}

} // namespace Lumix