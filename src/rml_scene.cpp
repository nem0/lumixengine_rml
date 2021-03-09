#define LUMIX_NO_CUSTOM_CRT
#include "rml_scene.h"
#include "RmlUi/Core.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/input_system.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/universe.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/texture.h"

namespace Lumix {

static const ComponentType RML_CANVAS_TYPE = reflection::getComponentType("rml_canvas");

struct RMLRenderJob : Renderer::RenderJob {
	struct Drawcall {
		u32 vertex_offset;
		u32 index_offset;
		u32 num_vertices;
		u32 num_indices;
		Rml::TextureHandle texture;
		Rml::Vector2f translation;
		IVec4 scissor;
		bool enable_scissor;
		bool is_3d;
		Vec3 pos;
		Quat rot;
	};

	struct TextureUpload {
		TextureUpload(IAllocator& allocator)
			: data(allocator) {}
		gpu::TextureHandle handle;
		Array<u8> data;
		u32 w;
		u32 h;
	};

	RMLRenderJob(IAllocator& allocator)
		: m_drawcalls(allocator)
		, m_vertices(allocator)
		, m_indices(allocator)
		, m_texture_uploads(allocator) {}

	void setup() override {}

	void execute() override {
		for (const TextureUpload& upload : m_texture_uploads) {
			gpu::createTexture(upload.handle, upload.w, upload.h, 1, gpu::TextureFormat::RGBA8, gpu::TextureFlags::NONE, upload.data.begin(), "rml_texture");
		}
		m_texture_uploads.clear();

		if (m_indices.empty()) return;

		gpu::pushDebugGroup("RML");
		gpu::BufferHandle vb = gpu::allocBufferHandle(); // TODO reuse
		gpu::BufferHandle ib = gpu::allocBufferHandle(); // TODO reuse
		gpu::createBuffer(vb, gpu::BufferFlags::NONE, m_vertices.byte_size(), m_vertices.begin());
		gpu::createBuffer(ib, gpu::BufferFlags::NONE, m_indices.byte_size(), m_indices.begin());
		gpu::StateFlags rs = gpu::getBlendStateBits(gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE);
		gpu::setState(rs);
		struct {
			Quat rot;
			Vec4 pos;
			Vec2 canvas_size;
			Rml::Vector2f translation;
		} data;
		data.canvas_size = m_canvas_size;
		for (const Drawcall& dc : m_drawcalls) {
			data.pos = Vec4(dc.pos, 1);
			data.rot = dc.rot;
			data.translation = dc.translation;

			gpu::useProgram(dc.is_3d ? m_program_3D : m_program_2D);

			gpu::TextureHandle t = (gpu::TextureHandle)dc.texture;
			gpu::bindTextures(&t, 0, 1);

			gpu::update(m_ub, &data, sizeof(data));
			gpu::bindUniformBuffer(4, m_ub, 0, sizeof(data));
			gpu::bindIndexBuffer(ib);
			gpu::bindVertexBuffer(0, vb, dc.vertex_offset * sizeof(Rml::Vertex), sizeof(Rml::Vertex));
			gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
			gpu::drawTriangles(dc.index_offset * sizeof(int), dc.num_indices, gpu::DataType::U32);
		}
		gpu::destroy(vb);
		gpu::destroy(ib);
		gpu::popDebugGroup();
	}

	gpu::ProgramHandle m_program_3D;
	gpu::ProgramHandle m_program_2D;
	gpu::BufferHandle m_ub;
	Vec2 m_canvas_size;
	Array<Drawcall> m_drawcalls;
	Array<Rml::Vertex> m_vertices;
	Array<int> m_indices;
	Array<TextureUpload> m_texture_uploads;
};

struct RenderInterface : Rml::RenderInterface {
	RenderInterface(Engine& engine, IAllocator& allocator)
		: m_allocator(allocator)
		, m_engine(engine) {}

	void RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rml::TextureHandle texture, const Rml::Vector2f& translation) override {
		RMLRenderJob::Drawcall& dc = m_job->m_drawcalls.emplace();
		dc.index_offset = m_job->m_indices.size();
		dc.vertex_offset = m_job->m_vertices.size();
		dc.num_indices = num_indices;
		dc.num_vertices = num_vertices;
		dc.texture = texture;
		dc.translation = translation;
		dc.scissor = m_scissor;
		dc.enable_scissor = m_scissor_enabled;
		dc.is_3d = m_is_3d;
		dc.rot = m_rot;
		dc.pos = m_pos;
		m_job->m_vertices.resize(m_job->m_vertices.size() + num_vertices);
		m_job->m_indices.resize(m_job->m_indices.size() + num_indices);
		memcpy(&m_job->m_vertices[dc.vertex_offset], vertices, sizeof(vertices[0]) * num_vertices);
		memcpy(&m_job->m_indices[dc.index_offset], indices, sizeof(indices[0]) * num_indices);
	}

	~RenderInterface() {
		if (m_shader) m_shader->decRefCount();
	}

	// Rml::CompiledGeometryHandle CompileGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rml::TextureHandle texture) { return {}; }
	// void RenderCompiledGeometry(Rml::CompiledGeometryHandle geometry, const Rml::Vector2f& translation) {}
	// void ReleaseCompiledGeometry(Rml::CompiledGeometryHandle geometry) {}

	void EnableScissorRegion(bool enable) override { m_scissor_enabled = enable; }

	void SetScissorRegion(int x, int y, int width, int height) override {
		m_scissor.x = x;
		m_scissor.y = y;
		m_scissor.z = width;
		m_scissor.w = height;
	}

	bool LoadTexture(Rml::TextureHandle& texture_handle, Rml::Vector2i& texture_dimensions, const Rml::String& source) override {
		Texture* t = m_engine.getResourceManager().load<Texture>(Path(source.c_str()));
		// unfortunately we have to do this until rml has API to query textures
		// TODO use Rml::GetTextureSourceList();
		while (t->isEmpty()) {
			m_engine.getFileSystem().processCallbacks();
			os::sleep(1);
		}
		if (t->isFailure()) return false;
		texture_dimensions.x = t->width;
		texture_dimensions.y = t->height;
		texture_handle = (uintptr_t)t->handle;
		return true;
	}

	bool GenerateTexture(Rml::TextureHandle& texture_handle, const Rml::byte* source, const Rml::Vector2i& source_dimensions) override {
		RMLRenderJob::TextureUpload& upload = m_job->m_texture_uploads.emplace(m_allocator);
		upload.handle = gpu::allocTextureHandle();
		upload.w = source_dimensions.x;
		upload.h = source_dimensions.y;
		upload.data.resize(source_dimensions.x * source_dimensions.y * 4);
		texture_handle = (uintptr_t)upload.handle;
		memcpy(upload.data.begin(), source, upload.data.byte_size());
		return true;
	}

	void ReleaseTexture(Rml::TextureHandle texture) override { ASSERT(false); }

	// void SetTransform(const Matrix4f* transform);

	void endRender() {
		gpu::VertexDecl decl;
		decl.addAttribute(0, 0, 2, gpu::AttributeType::FLOAT, 0);
		decl.addAttribute(1, 8, 4, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);
		decl.addAttribute(2, 12, 2, gpu::AttributeType::FLOAT, 0);
		m_job->m_program_2D = m_shader->getProgram(decl, 0);
		m_job->m_program_3D = m_shader->getProgram(decl, m_3D_define);
	}

	bool beginRender(Renderer& renderer, const Vec2& canvas_size, gpu::BufferHandle ub, bool is_3D, const Vec3& pos, const Quat& rot, IAllocator& allocator) {
		if (!m_shader->isReady()) return false;

		m_is_3d = is_3D;
		m_pos = pos;
		m_rot = rot;
		
		m_job = &renderer.createJob<RMLRenderJob>(allocator);
		m_job->m_ub = ub;
		m_job->m_canvas_size = canvas_size;
		m_scissor_enabled = false;
		return true;
	}

	IAllocator& m_allocator;
	Vec3 m_pos;
	Quat m_rot;
	bool m_is_3d;
	Engine& m_engine;
	u32 m_3D_define = 0;
	RMLRenderJob* m_job = nullptr;
	bool m_scissor_enabled = false;
	IVec4 m_scissor;
	Shader* m_shader = nullptr;
};

struct RMLSceneImpl : RMLScene {
	struct Canvas {
		EntityRef entity;
		bool is_3d = true;
		IVec2 virtual_size = {800, 600};
		Rml::Context* context;
	};

	RMLSceneImpl(IPlugin& plugin, Engine& engine, Universe& universe)
		: m_engine(engine)
		, m_plugin(plugin)
		, m_universe(universe)
		, m_canvases(engine.getAllocator())
		, m_render_interface(engine, engine.getAllocator())
	{}

	~RMLSceneImpl() override { ASSERT(m_canvases.empty()); }

	void createCanvas(EntityRef entity) {
		if (!m_focused.isValid()) m_focused = entity;
		Canvas& c = m_canvases.emplace();
		c.entity = entity;
		StaticString<64> context_name((u64)this, "#", entity.index);
		c.context = Rml::CreateContext(context_name.data, Rml::Vector2i(800, 600), &m_render_interface);
		OutputMemoryStream content(m_engine.getAllocator());
		if (m_engine.getFileSystem().getContentSync(Path("rml/demo.rml"), content)) {
			content.write((char)0);
			Rml::String str;
			str = (const char*)content.data();
			Rml::ElementDocument* doc = c.context->LoadDocumentFromMemory(str, "rml/demo.rml");
			if (doc) doc->Show();
		}
		m_universe.onComponentCreated(entity, RML_CANVAS_TYPE, this);
	}

	void destroyCanvas(EntityRef entity) {
		if (m_focused == entity) m_focused = INVALID_ENTITY;
		m_canvases.eraseItems([entity, this](const Canvas& c) {
			if (c.entity == entity) {
				StaticString<64> context_name((u64)this, "#", entity.index);
				Rml::RemoveContext(context_name.data);
				return true;
			}
			return false;
		});
		m_universe.onComponentDestroyed(entity, RML_CANVAS_TYPE, this);
	}

	Canvas* getCanvas(EntityRef e) {
		const int idx = m_canvases.find([e](const Canvas& c) { return c.entity == e; });
		return idx >= 0 ? &m_canvases[idx] : nullptr;
	}

	bool is3D(EntityRef e) override { return getCanvas(e)->is_3d; }

	void set3D(EntityRef e, bool is_3d) override { getCanvas(e)->is_3d = is_3d; }

	void render(Pipeline& pipeline) {
		if (!m_render_interface.m_shader) {
			m_render_interface.m_shader = m_engine.getResourceManager().load<Shader>(Path("pipelines/rml.shd"));
		}
		Renderer* renderer = static_cast<Renderer*>(m_engine.getPluginManager().getPlugin("renderer"));
		m_render_interface.m_3D_define = 1 << renderer->getShaderDefineIdx("SPATIAL");
		const Viewport vp = pipeline.getViewport();

		gpu::BufferHandle ub = pipeline.getDrawcallUniformBuffer();
		for (const Canvas& canvas : m_canvases) {
			const Vec2 canvas_size((float)vp.w, (float)vp.h);
			canvas.context->SetDimensions({vp.w, vp.h});
			const Transform& tr = m_universe.getTransform(canvas.entity);
			if (m_render_interface.beginRender(*renderer, canvas_size, ub, canvas.is_3d, Vec3(tr.pos - vp.pos), tr.rot, renderer->getAllocator())) {
				canvas.context->Render();
				m_render_interface.endRender();

				renderer->queue(*m_render_interface.m_job, 0);
				m_render_interface.m_job = nullptr;
			}
		}
	}

	void focus(EntityPtr e) { m_focused = e; }

	IVec2 transformMousePos(const Canvas& canvas, float x, float y) const {
		if (canvas.is_3d) {
			RenderScene* render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
			ASSERT(render_scene);
			EntityPtr cam_entity = render_scene->getActiveCamera();
			if (!cam_entity.isValid()) return IVec2((i32)x, (i32)y);

			const Camera& cam = render_scene->getCamera((EntityRef)cam_entity);
			const Viewport vp = render_scene->getCameraViewport((EntityRef)cam_entity);
			const DVec3 cam_pos = m_universe.getPosition((EntityRef)cam_entity);
			const DVec3 canvas_pos = m_universe.getPosition(canvas.entity);
			const Quat canvas_rot = m_universe.getRotation(canvas.entity);
			const Vec3 normal = canvas_rot.rotate(Vec3(0, 0, 1));
			DVec3 origin;
			Vec3 dir;
			vp.getRay({x, y}, origin, dir);
			float t;
			if (!getRayPlaneIntersecion(Vec3(origin - cam_pos), dir, Vec3(canvas_pos - cam_pos), normal, t)) return IVec2((i32)x, (i32)y);

			const DVec3 hit = origin + dir * t;
			const Vec3 xaxis = canvas_rot.rotate(Vec3(1, 0, 0));
			const Vec3 yaxis = canvas_rot.rotate(Vec3(0, 1, 0));

			const Vec3 rel_hit_pos = Vec3(hit - canvas_pos);
			float xproj = dot(rel_hit_pos, xaxis);
			float yproj = dot(rel_hit_pos, yaxis);
			return {int(xproj * vp.w), int((1 - yproj) * vp.h)};
		}

		return IVec2((i32)x, (i32)y);
	}

	void update(float time_delta, bool paused) override {
		const Canvas* focused = m_focused.isValid() ? getCanvas((EntityRef)m_focused) : nullptr;
		if (focused) {
			InputSystem& is = m_engine.getInputSystem();
			const InputSystem::Event* events = is.getEvents();
			for (i32 i = 0, c = is.getEventsCount(); i < c; ++i) {
				const InputSystem::Event& e = events[i];
				switch (e.type) {
					case InputSystem::Event::AXIS:
						if (e.device->type == InputSystem::Device::MOUSE) {
							const IVec2 mp = transformMousePos(*focused, e.data.axis.x_abs, e.data.axis.y_abs);
							focused->context->ProcessMouseMove(mp.x, mp.y, 0);
						}
						break;
					case InputSystem::Event::BUTTON:
						if (e.device->type == InputSystem::Device::MOUSE) {
							if (e.data.button.down) {
								focused->context->ProcessMouseButtonDown(0, 0);
							} else {
								focused->context->ProcessMouseButtonUp(0, 0);
							}
						}
						break;
				}
			}
		}
		for (const Canvas& c : m_canvases) c.context->Update();
	}

	void serialize(struct OutputMemoryStream& serializer) override {}
	void deserialize(struct InputMemoryStream& serialize, const struct EntityMap& entity_map, i32 version) override {}
	IPlugin& getPlugin() const override { return m_plugin; }
	struct Universe& getUniverse() override {
		return m_universe;
	}

	void clear() override {
		for (Canvas& c : m_canvases) {
			StaticString<64> context_name((u64)this, "#", c.entity.index);
			Rml::RemoveContext(context_name.data);
		}
		m_canvases.clear();
	}

	Engine& m_engine;
	IPlugin& m_plugin;
	EntityPtr m_focused = INVALID_ENTITY;
	RenderInterface m_render_interface;
	Universe& m_universe;
	Array<Canvas> m_canvases;
};

UniquePtr<RMLScene> RMLScene::create(IPlugin& plugin, Engine& engine, Universe& universe) {
	return UniquePtr<RMLSceneImpl>::create(engine.getAllocator(), plugin, engine, universe);
}

void RMLScene::reflect() {
	LUMIX_SCENE(RMLSceneImpl, "rml")
		.LUMIX_CMP(Canvas, "rml_canvas", "RML / Canvas")
			.prop<&RMLScene::is3D, &RMLScene::set3D>("Is 3D");
}

} // namespace Lumix