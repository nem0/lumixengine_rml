#define LUMIX_NO_CUSTOM_CRT
#include "rml_module.h"
#include "RmlUi/Core.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/input_system.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "renderer/draw_stream.h"
#include "renderer/pipeline.h"
#include "renderer/render_module.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/texture.h"

namespace Lumix {

static const ComponentType RML_CANVAS_TYPE = reflection::getComponentType("rml_canvas");

struct RmlRenderInterface : Rml::RenderInterface {
	RmlRenderInterface(Engine& engine, IAllocator& allocator)
		: m_allocator(allocator)
		, m_engine(engine) {}

	void RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rml::TextureHandle texture, const Rml::Vector2f& translation) override {
		if (!num_indices) return;
		
		struct UBData {
			Quat rot;
			Vec4 pos;
			Vec2 canvas_size;
			Rml::Vector2f translation;
		} data;
		Renderer::TransientSlice ub = m_renderer->allocUniform(sizeof(UBData));
		data.canvas_size = m_canvas_size;
		data.pos = Vec4(m_pos, 1);
		data.rot = m_rot;
		data.translation = translation;		
		memcpy(ub.ptr, &data, sizeof(data));

		gpu::BufferHandle vb = gpu::allocBufferHandle(); // TODO reuse
		gpu::BufferHandle ib = gpu::allocBufferHandle(); // TODO reuse

		const Renderer::MemRef vertices_mem = m_renderer->copy(vertices, num_vertices * sizeof(vertices[0]));
		const Renderer::MemRef indices_mem = m_renderer->copy(indices, num_indices * sizeof(indices[0]));

		m_draw_stream->createBuffer(vb, gpu::BufferFlags::NONE, vertices_mem.size, vertices_mem.data);
		m_draw_stream->createBuffer(ib, gpu::BufferFlags::NONE, indices_mem.size, indices_mem.data);
		m_draw_stream->useProgram(m_is_3d ? m_program_3D : m_program_2D);

		gpu::TextureHandle t = (gpu::TextureHandle)texture;
		m_draw_stream->bindTextures(&t, 0, 1);

		m_draw_stream->bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
		m_draw_stream->bindIndexBuffer(ib);
		m_draw_stream->bindVertexBuffer(0, vb, 0, sizeof(Rml::Vertex));
		m_draw_stream->bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
		m_draw_stream->drawIndexed(0, num_indices, gpu::DataType::U32);
		
		m_draw_stream->freeMemory(vertices_mem.data, m_renderer->getAllocator());
		m_draw_stream->freeMemory(indices_mem.data, m_renderer->getAllocator());
		
		m_draw_stream->destroy(vb);
		m_draw_stream->destroy(ib);
	}

	~RmlRenderInterface() {
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
		gpu::TextureHandle handle = gpu::allocTextureHandle();
		
		const Renderer::MemRef mem = m_renderer->copy(source, source_dimensions.x * source_dimensions.y * 4);

		m_draw_stream->createTexture(handle, source_dimensions.x, source_dimensions.y, 1, gpu::TextureFormat::RGBA8, gpu::TextureFlags::NONE, "rml_texture");
		m_draw_stream->update(handle, 0, 0, 0, 0, source_dimensions.x, source_dimensions.y, gpu::TextureFormat::RGBA8, mem.data, mem.size);
		m_draw_stream->freeMemory(mem.data, m_renderer->getAllocator());
		return true;
	}

	void ReleaseTexture(Rml::TextureHandle texture) override { ASSERT(false); }

	// void SetTransform(const Matrix4f* transform);

	bool beginRender(Renderer& renderer, const Vec2& canvas_size, bool is_3D, const Vec3& pos, const Quat& rot, IAllocator& allocator) {
		if (!m_shader->isReady()) return false;

		gpu::VertexDecl decl(gpu::PrimitiveType::TRIANGLES);
		decl.addAttribute(0, 0, 2, gpu::AttributeType::FLOAT, 0);
		decl.addAttribute(1, 8, 4, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);
		decl.addAttribute(2, 12, 2, gpu::AttributeType::FLOAT, 0);
		gpu::StateFlags rs = gpu::getBlendStateBits(gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE);
		m_program_2D = m_shader->getProgram(rs, decl, 0);
		m_program_3D = m_shader->getProgram(rs, decl, m_3D_define);
		m_is_3d = is_3D;
		m_pos = pos;
		m_rot = rot;
		
		m_draw_stream = &renderer.getDrawStream().createSubstream();
		m_renderer = &renderer;
		m_canvas_size = canvas_size;
		
		m_scissor_enabled = false;
		return true;
	}

	IAllocator& m_allocator;
	Vec3 m_pos;
	Quat m_rot;
	bool m_is_3d;
	Engine& m_engine;
	Renderer* m_renderer = nullptr;
	u32 m_3D_define = 0;
	DrawStream* m_draw_stream = nullptr;
	bool m_scissor_enabled = false;
	IVec4 m_scissor;
	Shader* m_shader = nullptr;
	Vec2 m_canvas_size;
	gpu::ProgramHandle m_program_3D;
	gpu::ProgramHandle m_program_2D;
};

struct RMLModuleImpl : RMLModule {
	struct Canvas {
		EntityRef entity;
		bool is_3d = true;
		IVec2 virtual_size = {800, 600};
		Rml::Context* context;
	};

	RMLModuleImpl(ISystem& system, Engine& engine, World& world)
		: m_engine(engine)
		, m_system(system)
		, m_world(world)
		, m_canvases(engine.getAllocator())
		, m_render_interface(engine, engine.getAllocator())
	{}

	~RMLModuleImpl() override {
		for (Canvas& c : m_canvases) {
			StaticString<64> context_name((u64)this, "#", c.entity.index);
			Rml::RemoveContext(context_name.data);
		}
	}

	const char* getName() const override { return "rml"; }

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
		m_world.onComponentCreated(entity, RML_CANVAS_TYPE, this);
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
		m_world.onComponentDestroyed(entity, RML_CANVAS_TYPE, this);
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
		Renderer& renderer = pipeline.getRenderer();
		m_render_interface.m_3D_define = 1 << renderer.getShaderDefineIdx("SPATIAL");
		const Viewport vp = pipeline.getViewport();

		for (const Canvas& canvas : m_canvases) {
			const Vec2 canvas_size((float)vp.w, (float)vp.h);
			canvas.context->SetDimensions({vp.w, vp.h});
			const Transform& tr = m_world.getTransform(canvas.entity);
			if (m_render_interface.beginRender(renderer, canvas_size, canvas.is_3d, Vec3(tr.pos - vp.pos), tr.rot, renderer.getAllocator())) {
				canvas.context->Render();
			}
		}
	}

	void focus(EntityPtr e) { m_focused = e; }

	IVec2 transformMousePos(const Canvas& canvas, float x, float y) const {
		if (canvas.is_3d) {
			RenderModule* render_module = static_cast<RenderModule*>(m_world.getModule("renderer"));
			ASSERT(render_module);
			EntityPtr cam_entity = render_module->getActiveCamera();
			if (!cam_entity.isValid()) return IVec2((i32)x, (i32)y);

			const Camera& cam = render_module->getCamera((EntityRef)cam_entity);
			const Viewport vp = render_module->getCameraViewport((EntityRef)cam_entity);
			const DVec3 cam_pos = m_world.getPosition((EntityRef)cam_entity);
			const DVec3 canvas_pos = m_world.getPosition(canvas.entity);
			const Quat canvas_rot = m_world.getRotation(canvas.entity);
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

	void update(float time_delta) override {
		const Canvas* focused = m_focused.isValid() ? getCanvas((EntityRef)m_focused) : nullptr;
		if (focused) {
			InputSystem& is = m_engine.getInputSystem();
			Span<const InputSystem::Event> events = is.getEvents();
			for (const InputSystem::Event& e : events) {
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
					default:
						// TODO text and stuff
						break;
				}
			}
		}
		for (const Canvas& c : m_canvases) c.context->Update();
	}

	void serialize(struct OutputMemoryStream& serializer) override {}
	void deserialize(struct InputMemoryStream& serialize, const struct EntityMap& entity_map, i32 version) override {}
	ISystem& getSystem() const override { return m_system; }
	struct World& getWorld() override {
		return m_world;
	}

	Engine& m_engine;
	ISystem& m_system;
	EntityPtr m_focused = INVALID_ENTITY;
	RmlRenderInterface m_render_interface;
	World& m_world;
	Array<Canvas> m_canvases;
};

UniquePtr<RMLModule> RMLModule::create(ISystem& system, Engine& engine, World& world) {
	return UniquePtr<RMLModuleImpl>::create(engine.getAllocator(), system, engine, world);
}

void RMLModule::reflect() {
	LUMIX_MODULE(RMLModuleImpl, "rml")
		.LUMIX_CMP(Canvas, "rml_canvas", "RML / Canvas")
			.prop<&RMLModule::is3D, &RMLModule::set3D>("Is 3D");
}

} // namespace Lumix