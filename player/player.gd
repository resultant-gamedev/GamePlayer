extends Panel

var games = []
onready var _game_list = get_node("GameList")
onready var dialog = get_node("FileDialog")

# load game list
func _enter_tree():
	var file = File.new()
	if OK == file.open("user://games.json", File.READ):
		var _games = file.get_var()
		if _games != null:
			self.games = _games

# save game list
func _save():
	var file = File.new()
	if OK == file.open("user://games.json", File.WRITE):
		file.store_var(games)

func _exit_tree():
	_save()	


func _ready():
	_game_list.connect("item_selected", self, "_game_selected")
	_update_list()
	dialog.connect("file_selected", self, "_file_selected")
	dialog.connect("dir_selected", self, "_dir_selected")
	get_node("Buttons/play").connect("pressed", self, "_play")
	get_node("Buttons/import").connect("pressed", self, "_import")
	get_node("Buttons/scan").connect("pressed", self, "_scan")
	get_node("Buttons/exit").connect("pressed", self, "_exit")
	get_node("Buttons/clear").connect("pressed", self, "_clear")
	get_node("Controls/allowzip").connect("pressed",self, "_update_list")
	get_node("Filter2/value").connect("text_changed", self, "_search_games")
	
func _update_list():
	_game_list.clear()
	for g in games:
		if not get_node("Controls/allowzip").is_pressed() and g.ends_with(".zip"):
			continue
		_game_list.add_item(g)

func _search_games(text):
	_game_list.clear()
	for g in games:
		if not get_node("Controls/allowzip").is_pressed() and g.ends_with(".zip"):
			continue
		if text.is_subsequence_of(g):
			_game_list.add_item(g)

func _file_selected(path):
	var game_path = ""
	if not path.empty() and path.length() > 4:
		if path.ends_with("engine.cfg"):
			game_path = path.get_base_dir()
		elif path.ends_with(".pck"):
			game_path = path
		elif get_node("Controls/allowzip").is_pressed() and path.ends_with(".zip"):
			game_path = path
	if not game_path.empty():
		_add_game(game_path)
	else:
		OS.alert(str("Invalid game: ", path), "Error")

func _dir_selected(dir):
	var files = get_files_in_dir(dir, false, true)
	for f in files:
		if f.ends_with("engine.cfg"):
			_add_game(f.get_base_dir())
		elif f.ends_with(".pck"):
			_add_game(f)
		elif get_node("Controls/allowzip").is_pressed() and f.ends_with(".zip"):
			_add_game(f)

func _add_game(path):
	if not (path in games):
		games.append(path)
		_game_list.add_item(path)
		_save()

func _game_selected(index):
	var path = _game_list.get_item_text(index)
	get_node("SelectedInfo/value").set_text(path)

func _import():
	dialog.set_title("Import Game")
	dialog.set_access(FileDialog.ACCESS_FILESYSTEM)
	dialog.set_mode(FileDialog.MODE_OPEN_FILE)
	dialog.clear_filters()
	dialog.add_filter("engine.cfg;Godot game")
	if get_node("Controls/allowzip").is_pressed():
		dialog.add_filter("*.zip;Godot game package")
	dialog.add_filter("*.pck;Godot game package")
	dialog.popup()
	
func _scan():
	dialog.set_title("Scan games from folder")
	dialog.set_access(FileDialog.ACCESS_FILESYSTEM)
	dialog.set_mode(FileDialog.MODE_OPEN_DIR)
	dialog.popup()

func _exit():
	get_tree().quit()

func _clear():
	games.clear()
	_game_list.clear()
	_save()
	
func _play():
	var path = get_node("SelectedInfo/value").get_text()
	if path.empty():
		return
	if path.ends_with("engine.cfg"):
		start_project(path.get_base_dir())
	else:
		start_project(path)

# Load project and restart the scenetree if possible
# @param path:String the project path to start with
#	Tt could be a project folder which contains the engine.cfg file
#	Or it could be the pck/zip file that packed from godot project
func start_project(path):
	PathRemap.clear_remaps()
	var scene_tree = get_tree()
	var show_collision_hint = get_node("Controls/show_collisions").is_pressed()
	var show_navigation_hint = get_node("Controls/show_navigation").is_pressed()
	var manager = SceneTreeManager.new()
	if OK == manager.load_project(path):
		remap_path(path)
		if OK == manager.restart_scene_tree():
			scene_tree.set_debug_collisions_hint(show_collision_hint)
			scene_tree.set_debug_navigation_hint(show_navigation_hint)
		else:
			OS.alert(str("Failed start game from: ", path), "Error")
	else:
		OS.alert(str("Failed load game from: ", path), "Error")

# remap all files under target path as files under res://
# Means the folder targetPath is same with res://
# @param targetPath: String the directory of the folder to remap with
func remap_path(targetPath):
	var dir = Directory.new()
	if OK == dir.open(targetPath):
		print(">>>>>>>>>> Remap 'res://' to '", targetPath, "' <<<<<<<<<<<")
		var pathes = get_files_in_dir(targetPath, true, true)
		for abspath in pathes:
			var rpath = abspath.replace(targetPath, "")
			if rpath.begins_with("/"):
				rpath = rpath.substr(1, rpath.length())
			var respath = str("res://", rpath)
			print(abspath, " => ", respath)
			PathRemap.add_remap(respath, abspath)
		

# Get file pathes in a list under target folder
# @param path:String The folder to search from
# @param with_dirs:boolean = false Includes directories
# @param recurse:boolean = false Search sub-folders recursely
func get_files_in_dir(path, with_dirs=false,recurse=false):
	var files = []
	var dir = Directory.new()
	if dir.open(path) == OK:
		dir.list_dir_begin()
		var file_name = dir.get_next()
		while not file_name.empty():
			if dir.current_is_dir() and not (file_name in [".", "..", "./"]):
				if recurse:
					var childfiles = self.get_files_in_dir(str(path, "/", file_name), with_dirs, recurse)
					for f in childfiles:
						files.append(f)
			if not (file_name in [".", ".."]):
				var rpath = path
				if rpath.ends_with("/"):
					pass
				elif rpath == ".":
					rpath = ""
				else:
					rpath += "/"
				if not with_dirs and dir.current_is_dir():
					pass
				else:
					rpath = str(rpath, file_name).replace("/./", "/")
					files.append(rpath)
			file_name = dir.get_next()
	return files