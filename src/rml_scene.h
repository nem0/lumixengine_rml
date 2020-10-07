#pragma once

#include "engine/plugin.h"

namespace Lumix {

struct RMLScene : IScene {
	virtual bool is3D(EntityRef e) = 0;
	virtual void set3D(EntityRef e, bool is_3d) = 0;
	virtual void render(struct Pipeline& pipeline) = 0;

	static RMLScene* create(IPlugin& plugin, Engine& engine, Universe& universe);
};

} // namespace Lumix