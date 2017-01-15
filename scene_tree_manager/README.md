## Scene Tree Manager for Godot

Load anothor game from resource package file or project folder after the game started.

Class `SceneTreeManager` is designed to load and restart game from resource files.

**You have to change the godot's source code below:**

In file `core/resource.h` add `SceneTreeManager` to the friend class of the class `ResourceCache`.
```c++
class ResourceCache {
friend class Resource;
friend class SceneTreeManager; // Add this line
...
```

Then you can load and change the game from resources in GDScript after your game started.

```gdscript
# The path can be a resource file exported from godot editor
# It could also be a godot project folder path
func start_project(path):
	var scene_tree = get_tree()
	var manager = SceneTreeManager.new()
	if OK == manager.load_project(path):
		if OK == manager.restart_scene_tree():
			scene_tree.set_debug_collisions_hint(true)
			scene_tree.set_debug_navigation_hint(false)
```

Here is an full example [player.gd](../player/player.gd)
