#pragma once

#include "engine/plugin.h"

namespace Lumix {

struct RMLModule : IModule {
	virtual bool is3D(EntityRef e) = 0;
	virtual void set3D(EntityRef e, bool is_3d) = 0;
	virtual void render(struct Pipeline& pipeline) = 0;

	static UniquePtr<RMLModule> create(ISystem& system, Engine& engine, World& world);
	static void reflect();
};

} // namespace Lumix