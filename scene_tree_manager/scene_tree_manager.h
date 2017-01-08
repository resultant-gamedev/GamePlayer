#ifndef SCENE_TREE_CHANGER_H
#define SCENE_TREE_CHANGER_H

#include <core/reference.h>

class SceneTreeManager : public Reference
{
	OBJ_TYPE(SceneTreeManager, Reference);
protected:
	static void _bind_methods();

public:
	Error restart_scene_tree() const;
	Error open_project(const String& p_path) const;
	Error load_global_settings(const String &p_path) const;
	Error load_binary_global_settings(const String &p_path) const;
	Error load_project(const String &p_path) const;

	SceneTreeManager();
};

#endif // SCENE_TREE_CHANGER_H
