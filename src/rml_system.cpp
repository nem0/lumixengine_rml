#define LUMIX_NO_CUSTOM_CRT
#include "rml_system.h"
#include "RmlUi/Core.h"
#include "engine/allocator.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/plugin.h"
#include "engine/reflection.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "rml_scene.h"

namespace Lumix {

struct RMLRenderPlugin : RenderPlugin {
	void renderUI(Pipeline& pipeline) override {
		Universe& universe = pipeline.getScene()->getUniverse();
		RMLScene* scene = static_cast<RMLScene*>(universe.getScene(crc32("RML")));
		scene->render(pipeline);
	}
};

struct SystemInterface : Rml::SystemInterface {
	double GetElapsedTime() override { return m_timer.getTimeSinceStart(); }
	// int TranslateString(String& translated, const String& input);
	// void JoinPath(String& translated_path, const String& document_path, const String& path);
	bool LogMessage(Rml::Log::Type type, const Rml::String& message) {
		if (type == Rml::Log::LT_WARNING)
			logWarning("Rml") << message.c_str();
		else if (type == Rml::Log::LT_ERROR)
			logError("Rml") << message.c_str();
		else
			logInfo("Rml") << message.c_str();
		return true;
	}
	// void SetMouseCursor(const String& cursor_name);
	// void SetClipboardText(const String& text);
	// void GetClipboardText(String& text);
	// void ActivateKeyboard();
	// void DeactivateKeyboard();
	OS::Timer m_timer;
};

struct RMLSystem : IPlugin {
	RMLSystem(Engine& engine)
		: m_engine(engine) {
		using namespace Reflection;
		static auto rml_scene = scene("rml", component("rml_canvas", property("Is 3D", &RMLScene::is3D, &RMLScene::set3D)));
		registerScene(rml_scene);
	}

	~RMLSystem() override {
		IPlugin* renderer = m_engine.getPluginManager().getPlugin("renderer");
		if (renderer) {
			((Renderer*)renderer)->removePlugin(m_render_plugin);
		}
		Rml::Shutdown();
	}

	void pluginAdded(IPlugin& plugin) override {
		if (equalStrings(plugin.getName(), "renderer")) {
			Renderer& r = (Renderer&)plugin;
			r.addPlugin(m_render_plugin);
		}
	}


	const char* getName() const override { return "RML"; }
	u32 getVersion() const override { return 0; }
	void serialize(OutputMemoryStream& serializer) const override {}
	bool deserialize(u32 version, InputMemoryStream& serializer) override { return true; }

	void init() override {
		Rml::SetSystemInterface(&m_system_interface);
		Rml::Initialise();
		// Rml::LoadFontFace("editor/fonts/NotoSans-Regular.ttf", true);
		Rml::LoadFontFace("rml/Delicious-Bold.otf");
		Rml::LoadFontFace("rml/Delicious-BoldItalic.otf");
		Rml::LoadFontFace("rml/Delicious-Italic.otf");
		Rml::LoadFontFace("rml/Delicious-Roman.otf");
	}

	void createScenes(Universe& universe) override { RMLScene::create(*this, m_engine, universe); }
	void destroyScene(IScene* scene) override { LUMIX_DELETE(m_engine.getAllocator(), scene); }

	Engine& m_engine;
	SystemInterface m_system_interface;
	RMLRenderPlugin m_render_plugin;
};

IPlugin* createRMLSystem(Engine& engine) {
	return LUMIX_NEW(engine.getAllocator(), RMLSystem)(engine);
}

LUMIX_PLUGIN_ENTRY(rml) {
	return createRMLSystem(engine);
}

} // namespace Lumix