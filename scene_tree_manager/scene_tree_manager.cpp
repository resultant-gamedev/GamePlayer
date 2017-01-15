#include "scene_tree_manager.h"
#include <scene/main/scene_main_loop.h>
#include <core/translation.h>
#include <core/os/os.h>
#include <os/file_access.h>
#include <os/dir_access.h>
#include <core/os/keyboard.h>
#include <core/io/resource_loader.h>
#include <core/script_language.h>
#include <scene/resources/packed_scene.h>
#include <scene/main/viewport.h>
#include <core/error_macros.h>
#include <core/resource.h>
#include <core/list.h>
#include <core/input_map.h>
#include <core/message_queue.h>
#include <core/path_remap.h>
#include <main/input_default.h>
#include <core/translation.h>
#include <core/io/marshalls.h>
#include <core/globals.h>

static Variant _decode_variant(const String& p_string);

SceneTreeManager::SceneTreeManager():Reference() {

}

void SceneTreeManager::_bind_methods() {
	ObjectTypeDB::bind_method(_MD("restart_scene_tree"), &SceneTreeManager::restart_scene_tree);
	ObjectTypeDB::bind_method(_MD("load_project", "path"), &SceneTreeManager::load_project);
}

Error SceneTreeManager::restart_scene_tree() const {
	SceneTree * scenetree = SceneTree::get_singleton();
	String game_path=GLOBAL_DEF("application/main_scene","");
	if(game_path.empty()) {
		ERR_EXPLAIN("Empty loading game path");
		return FAILED;
	}
	else {

		String stretch_mode = GLOBAL_DEF("display/stretch_mode","disabled");
		String stretch_aspect = GLOBAL_DEF("display/stretch_aspect","ignore");
		Size2i stretch_size = Size2(GLOBAL_DEF("display/width",0),GLOBAL_DEF("display/height",0));

		SceneTree::StretchMode sml_sm=SceneTree::STRETCH_MODE_DISABLED;
		if (stretch_mode=="2d")
			sml_sm=SceneTree::STRETCH_MODE_2D;
		else if (stretch_mode=="viewport")
			sml_sm=SceneTree::STRETCH_MODE_VIEWPORT;

		SceneTree::StretchAspect sml_aspect=SceneTree::STRETCH_ASPECT_IGNORE;
		if (stretch_aspect=="keep")
			sml_aspect=SceneTree::STRETCH_ASPECT_KEEP;
		else if (stretch_aspect=="keep_width")
			sml_aspect=SceneTree::STRETCH_ASPECT_KEEP_WIDTH;
		else if (stretch_aspect=="keep_height")
			sml_aspect=SceneTree::STRETCH_ASPECT_KEEP_HEIGHT;

		scenetree->set_screen_stretch(sml_sm,sml_aspect,stretch_size);
		if(stretch_size != Size2())
			OS::get_singleton()->set_window_size(stretch_size);

		scenetree->set_auto_accept_quit(GLOBAL_DEF("application/auto_accept_quit",true));
		String appname = Globals::get_singleton()->get("application/name");
		appname = TranslationServer::get_singleton()->translate(appname);
		OS::get_singleton()->set_window_title(appname);


		List<PropertyInfo> props;
		Globals::get_singleton()->get_property_list(&props);

		String local_game_path=game_path.replace("\\","/");
		if (!local_game_path.begins_with("res://")) {
			bool absolute=(local_game_path.size()>1) && (local_game_path[0]=='/' || local_game_path[1]==':');

			if (!absolute) {

				if (Globals::get_singleton()->is_using_datapack()) {

					local_game_path="res://"+local_game_path;

				} else {
					int sep=local_game_path.find_last("/");

					if (sep==-1) {
						DirAccess *da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
						local_game_path=da->get_current_dir()+"/"+local_game_path;
						memdelete(da);
					} else {

						DirAccess *da = DirAccess::open(local_game_path.substr(0,sep));
						if (da) {
							local_game_path=da->get_current_dir()+"/"+local_game_path.substr(sep+1,local_game_path.length());;
							memdelete(da);
						}
					}
				}

			}
		}
		local_game_path=Globals::get_singleton()->localize_path(local_game_path);

		//first pass, add the constants so they exist before any script is loaded
		for(List<PropertyInfo>::Element *E=props.front();E;E=E->next()) {

			String s = E->get().name;
			if (!s.begins_with("autoload/"))
				continue;
			String name = s.get_slicec('/',1);
			String path = Globals::get_singleton()->get(s);
			bool global_var=false;
			if (path.begins_with("*")) {
				global_var=true;
			}

			if (global_var) {
				for(int i=0;i<ScriptServer::get_language_count();i++) {
					ScriptServer::get_language(i)->add_global_constant(name,Variant());
				}
			}

		}

		//second pass, load into global constants
		List<Node*> to_add;
		for(List<PropertyInfo>::Element *E=props.front();E;E=E->next()) {

			String s = E->get().name;
			if (!s.begins_with("autoload/"))
				continue;
			String name = s.get_slicec('/',1);
			String path = Globals::get_singleton()->get(s);
			bool global_var=false;
			if (path.begins_with("*")) {
				global_var=true;
				path=path.substr(1,path.length()-1);
			}

			RES res = ResourceLoader::load(path);
			ERR_EXPLAIN("Can't autoload: "+path);
			ERR_CONTINUE(res.is_null());
			Node *n=NULL;
			if (res->is_type("PackedScene")) {
				Ref<PackedScene> ps = res;
				n=ps->instance();
			} else if (res->is_type("Script")) {
				Ref<Script> s = res;
				StringName ibt = s->get_instance_base_type();
				bool valid_type = ObjectTypeDB::is_type(ibt,"Node");
				ERR_EXPLAIN("Script does not inherit a Node: "+path);
				ERR_CONTINUE( !valid_type );

				Object *obj = ObjectTypeDB::instance(ibt);

				ERR_EXPLAIN("Cannot instance script for autoload, expected 'Node' inheritance, got: "+String(ibt));
				ERR_CONTINUE( obj==NULL );

				n = obj->cast_to<Node>();
				n->set_script(s.get_ref_ptr());
			}

			ERR_EXPLAIN("Path in autoload not a node or script: "+path);
			ERR_CONTINUE(!n);
			n->set_name(name);

			//defer so references are all valid on _ready()
			//sml->get_root()->add_child(n);
			to_add.push_back(n);

			if (global_var) {
				for(int i=0;i<ScriptServer::get_language_count();i++) {
					ScriptServer::get_language(i)->add_global_constant(name,n);
				}
			}

		}

		for(List<Node*>::Element *E=to_add.front();E;E=E->next()) {
			scenetree->get_root()->add_child(E->get());
		}

		Node *scene=NULL;
		Ref<PackedScene> scenedata = ResourceLoader::load(local_game_path);
		if (scenedata.is_valid())
			scene=scenedata->instance();

		ERR_EXPLAIN("Failed loading scene: "+local_game_path);
		ERR_FAIL_COND_V(!scene, FAILED);


		String iconpath = GLOBAL_DEF("application/icon","Variant()""");
		if (iconpath!="") {
			Image icon;
			if (icon.load(iconpath)==OK)
				OS::get_singleton()->set_icon(icon);
		}

		Node *curscene = scenetree->get_current_scene();
		if(curscene)
			curscene->queue_delete();

		scenetree->add_current_scene(scene);
	}
	return OK;
}

// Most of code below are copied from global.cpp

Error SceneTreeManager::load_global_settings(const String &p_path) const {
	Error err;
	ResourceCache::clear();
	FileAccess *f= FileAccess::open(p_path,FileAccess::READ,&err);

	if (err!=OK) {

		return err;
	}

	String line;
	String section;
	String subpath;

	Globals* globals = Globals::get_singleton();
	globals->set_registering_order(false);

	int line_count = 0;

	while(!f->eof_reached()) {

		String line = f->get_line().strip_edges();
		line_count++;

		if (line=="")
			continue;

		// find comments

		{

			int pos=0;
			while (true) {
				int ret = line.find(";",pos);
				if (ret==-1)
					break;

				int qc=0;
				for(int i=0;i<ret;i++) {

					if (line[i]=='"')
						qc++;
				}

				if ( !(qc&1) ) {
					//not inside string, real comment
					line=line.substr(0,ret);
					break;

				}

				pos=ret+1;


			}
		}

		if (line.begins_with("[")) {

			int end = line.find_last("]");
			ERR_CONTINUE(end!=line.length()-1);

			String section=line.substr(1,line.length()-2);

			if (section=="global" || section == "")
				subpath="";
			else
				subpath=section+"/";

		} else if (line.find("=")!=-1) {


			int eqpos = line.find("=");
			String var=line.substr(0,eqpos).strip_edges();
			String value=line.substr(eqpos+1,line.length()).strip_edges();

			Variant val = _decode_variant(value);

			globals->set(subpath+var,val);
			globals->set_persisting(subpath+var,true);
			//props[subpath+var]=VariantContainer(val,last_order++,true);

		} else {

			if (line.length() > 0) {
				ERR_PRINT(String("Syntax error on line "+itos(line_count)+" of file "+p_path).ascii().get_data());
			};
		};
	}

	memdelete(f);

	globals->set_registering_order(true);

	return OK;
}

Error SceneTreeManager::load_binary_global_settings(const String& p_path) const {
	Error err;
	ResourceCache::clear();
	FileAccess *f= FileAccess::open(p_path,FileAccess::READ,&err);
	if (err!=OK) {
		return err;
	}

	uint8_t hdr[4];
	f->get_buffer(hdr,4);
	if (hdr[0]!='E'|| hdr[1]!='C' || hdr[2]!='F' || hdr[3]!='G') {

		memdelete(f);
		ERR_EXPLAIN("Corrupted header in binary engine.cfb (not ECFG)");
		ERR_FAIL_V(ERR_FILE_CORRUPT;)
	}

	Globals::get_singleton()->set_registering_order(false);

	uint32_t count=f->get_32();

	for(int i=0;i<count;i++) {

		uint32_t slen=f->get_32();
		CharString cs;
		cs.resize(slen+1);
		cs[slen]=0;
		f->get_buffer((uint8_t*)cs.ptr(),slen);
		String key;
		key.parse_utf8(cs.ptr());

		uint32_t vlen=f->get_32();
		Vector<uint8_t> d;
		d.resize(vlen);
		f->get_buffer(d.ptr(),vlen);
		Variant value;
		Error err = decode_variant(value,d.ptr(),d.size());
		ERR_EXPLAIN("Error decoding property: "+key);
		ERR_CONTINUE(err!=OK);
		Globals::get_singleton()->set(key,value);
		Globals::get_singleton()->set_persisting(key,true);
	}

	Globals::get_singleton()->set_registering_order(true);


	return OK;
}

static Vector<String> _decode_params(const String& p_string) {

	int begin=p_string.find("(");
	ERR_FAIL_COND_V(begin==-1,Vector<String>());
	begin++;
	int end=p_string.find(")");
	ERR_FAIL_COND_V(end<begin,Vector<String>());
	return p_string.substr(begin,end-begin).split(",");
}

static String _get_chunk(const String& str,int &pos, int close_pos) {


	enum {
		MIN_COMMA,
		MIN_COLON,
		MIN_CLOSE,
		MIN_QUOTE,
		MIN_PARENTHESIS,
		MIN_CURLY_OPEN,
		MIN_OPEN
	};

	int min_pos=close_pos;
	int min_what=MIN_CLOSE;

#define TEST_MIN(m_how,m_what) \
	{\
	int res = str.find(m_how,pos);\
	if (res!=-1 && res < min_pos) {\
	min_pos=res;\
	min_what=m_what;\
}\
}\


	TEST_MIN(",",MIN_COMMA);
	TEST_MIN("[",MIN_OPEN);
	TEST_MIN("{",MIN_CURLY_OPEN);
	TEST_MIN("(",MIN_PARENTHESIS);
	TEST_MIN("\"",MIN_QUOTE);

	int end=min_pos;


	switch(min_what) {

	case MIN_COMMA: {
		} break;
	case MIN_CLOSE: {
			//end because it's done
		} break;
	case MIN_QUOTE: {
			end=str.find("\"",min_pos+1)+1;
			ERR_FAIL_COND_V(end==-1,Variant());

		} break;
	case MIN_PARENTHESIS: {

			end=str.find(")",min_pos+1)+1;
			ERR_FAIL_COND_V(end==-1,Variant());

		} break;
	case MIN_OPEN: {
			int level=1;
			end++;
			while(end<close_pos) {

				if (str[end]=='[')
					level++;
				if (str[end]==']') {
					level--;
					if (level==0)
						break;
				}
				end++;
			}
			ERR_FAIL_COND_V(level!=0,Variant());
			end++;
		} break;
	case MIN_CURLY_OPEN: {
			int level=1;
			end++;
			while(end<close_pos) {

				if (str[end]=='{')
					level++;
				if (str[end]=='}') {
					level--;
					if (level==0)
						break;
				}
				end++;
			}
			ERR_FAIL_COND_V(level!=0,Variant());
			end++;
		} break;

	}

	String ret = str.substr(pos,end-pos);

	pos=end;
	while(pos<close_pos) {
		if (str[pos]!=',' && str[pos]!=' ' && str[pos]!=':')
			break;
		pos++;
	}

	return ret;

}



static Variant _decode_variant(const String& p_string) {


	String str = p_string.strip_edges();

	if (str.nocasecmp_to("true")==0)
		return Variant(true);
	if (str.nocasecmp_to("false")==0)
		return Variant(false);
	if (str.nocasecmp_to("nil")==0)
		return Variant();
	if (str.is_valid_float()) {
		if (str.find(".")==-1)
			return str.to_int();
		else
			return str.to_double();

	}
	if (str.begins_with("#")) { //string
		return Color::html(str);
	}
	if (str.begins_with("\"")) { //string
		int end = str.find_last("\"");
		ERR_FAIL_COND_V(end==0,Variant());
		return str.substr(1,end-1).xml_unescape();

	}

	if (str.begins_with("[")) { //array

		int close_pos = str.find_last("]");
		ERR_FAIL_COND_V(close_pos==-1,Variant());
		Array array;

		int pos=1;

		while(pos<close_pos) {

			String s = _get_chunk(str,pos,close_pos);
			array.push_back(_decode_variant(s));
		}
		return array;

	}

	if (str.begins_with("{")) { //array

		int close_pos = str.find_last("}");
		ERR_FAIL_COND_V(close_pos==-1,Variant());
		Dictionary d;

		int pos=1;

		while(pos<close_pos) {

			String key = _get_chunk(str,pos,close_pos);
			String data = _get_chunk(str,pos,close_pos);
			d[_decode_variant(key)]=_decode_variant(data);
		}
		return d;

	}
	if (str.begins_with("key")) {
		Vector<String> params = _decode_params(p_string);
		ERR_FAIL_COND_V(params.size()!=1 && params.size()!=2,Variant());
		int scode=0;

		if (params[0].is_numeric()) {
			scode=params[0].to_int();
			if (scode<10)
				scode+=KEY_0;
		} else
			scode=find_keycode(params[0]);

		InputEvent ie;
		ie.type=InputEvent::KEY;
		ie.key.scancode=scode;

		if (params.size()==2) {
			String mods=params[1];
			if (mods.findn("C")!=-1)
				ie.key.mod.control=true;
			if (mods.findn("A")!=-1)
				ie.key.mod.alt=true;
			if (mods.findn("S")!=-1)
				ie.key.mod.shift=true;
			if (mods.findn("M")!=-1)
				ie.key.mod.meta=true;
		}
		return ie;

	}

	if (str.begins_with("mbutton")) {
		Vector<String> params = _decode_params(p_string);
		ERR_FAIL_COND_V(params.size()!=2,Variant());

		InputEvent ie;
		ie.type=InputEvent::MOUSE_BUTTON;
		ie.device=params[0].to_int();
		ie.mouse_button.button_index=params[1].to_int();

		return ie;
	}

	if (str.begins_with("jbutton")) {
		Vector<String> params = _decode_params(p_string);
		ERR_FAIL_COND_V(params.size()!=2,Variant());

		InputEvent ie;
		ie.type=InputEvent::JOYSTICK_BUTTON;
		ie.device=params[0].to_int();
		ie.joy_button.button_index=params[1].to_int();

		return ie;
	}

	if (str.begins_with("jaxis")) {
		Vector<String> params = _decode_params(p_string);
		ERR_FAIL_COND_V(params.size()!=2,Variant());

		InputEvent ie;
		ie.type=InputEvent::JOYSTICK_MOTION;
		ie.device=params[0].to_int();
		int axis = params[1].to_int();;
		ie.joy_motion.axis=axis>>1;
		ie.joy_motion.axis_value=axis&1?1:-1;

		return ie;
	}
	if (str.begins_with("img")) {
		Vector<String> params = _decode_params(p_string);
		if (params.size()==0) {
			return Image();
		}

		ERR_FAIL_COND_V(params.size()!=5,Image());

		String format=params[0].strip_edges();

		Image::Format imgformat;

		if (format=="grayscale") {
			imgformat=Image::FORMAT_GRAYSCALE;
		} else if (format=="intensity") {
			imgformat=Image::FORMAT_INTENSITY;
		} else if (format=="grayscale_alpha") {
			imgformat=Image::FORMAT_GRAYSCALE_ALPHA;
		} else if (format=="rgb") {
			imgformat=Image::FORMAT_RGB;
		} else if (format=="rgba") {
			imgformat=Image::FORMAT_RGBA;
		} else if (format=="indexed") {
			imgformat=Image::FORMAT_INDEXED;
		} else if (format=="indexed_alpha") {
			imgformat=Image::FORMAT_INDEXED_ALPHA;
		} else if (format=="bc1") {
			imgformat=Image::FORMAT_BC1;
		} else if (format=="bc2") {
			imgformat=Image::FORMAT_BC2;
		} else if (format=="bc3") {
			imgformat=Image::FORMAT_BC3;
		} else if (format=="bc4") {
			imgformat=Image::FORMAT_BC4;
		} else if (format=="bc5") {
			imgformat=Image::FORMAT_BC5;
		} else if (format=="custom") {
			imgformat=Image::FORMAT_CUSTOM;
		} else {

			ERR_FAIL_V( Image() );
		}

		int mipmaps=params[1].to_int();
		int w=params[2].to_int();
		int h=params[3].to_int();

		if (w == 0 && h == 0) {
			//r_v = Image(w, h, imgformat);
			return Image();
		};


		String data=params[4];
		int datasize=data.length()/2;
		DVector<uint8_t> pixels;
		pixels.resize(datasize);
		DVector<uint8_t>::Write wb = pixels.write();
		const CharType *cptr=data.c_str();

		int idx=0;
		uint8_t byte;
		while( idx<datasize*2) {

			CharType c=*(cptr++);

			ERR_FAIL_COND_V(c=='<',ERR_FILE_CORRUPT);

			if ( (c>='0' && c<='9') || (c>='A' && c<='F') || (c>='a' && c<='f') ) {

				if (idx&1) {

					byte|=HEX2CHR(c);
					wb[idx>>1]=byte;
				} else {

					byte=HEX2CHR(c)<<4;
				}

				idx++;
			}

		}

		wb = DVector<uint8_t>::Write();

		return Image(w,h,mipmaps,imgformat,pixels);
	}

	if (str.find(",")!=-1) { //vector2 or vector3
		Vector<float> farr = str.split_floats(",",true);
		if (farr.size()==2) {
			return Point2(farr[0],farr[1]);
		}
		if (farr.size()==3) {
			return Vector3(farr[0],farr[1],farr[2]);
		}
		ERR_FAIL_V(Variant());
	}


	return Variant();
}

Error SceneTreeManager::load_project(const String &p_path) const {
	Error err;
	auto globals = Globals::get_singleton();
	DirAccess* dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	FileAccess* f = FileAccess::create(FileAccess::ACCESS_FILESYSTEM);
	if( OK == dir->change_dir(p_path)  ){
		String cfg_path = p_path + "/engine.cfg";
		if(!f->file_exists(cfg_path))
			cfg_path = p_path + "/engine.cfb";
		if(!f->file_exists(cfg_path)){
			memdelete(f);
			memdelete(dir);
			ERR_EXPLAIN("engine.cfg not found under project directory.");
			return FAILED;
		}
		if(cfg_path.ends_with(".cfg"))
			err = load_global_settings(cfg_path);
		else if(cfg_path.ends_with(".cfb"))
			err = load_binary_global_settings(cfg_path);
		else
			err = FAILED;
	}
	else if(f->file_exists(p_path)) {
		if(globals->call("load_resource_pack", p_path))
			err = load_binary_global_settings("res://engine.cfb");
		else
			err = FAILED;
	}
	else
		err = FAILED;

	memdelete(f);
	memdelete(dir);

	if (OK != err)
		return err;
	_print_error_enabled = bool(GLOBAL_DEF("application/disable_stderr", true));
	_print_line_enabled  = bool(GLOBAL_DEF("application/disable_stdout", true));

	//keys for game
	InputMap *input_map = InputMap::get_singleton();
	input_map->load_from_globals();


	OS::VideoMode video_mode;
	bool use_custom_res=true;
	bool force_res=false;
	if (!force_res && use_custom_res && globals->has("display/width"))
		video_mode.width=globals->get("display/width");
	if (!force_res &&use_custom_res && globals->has("display/height"))
		video_mode.height=globals->get("display/height");
	if (use_custom_res && globals->has("display/fullscreen"))
		video_mode.fullscreen=globals->get("display/fullscreen");
	if (use_custom_res && globals->has("display/resizable"))
		video_mode.resizable=globals->get("display/resizable");
	if (use_custom_res && globals->has("display/borderless_window"))
		video_mode.borderless_window = globals->get("display/borderless_window");

	if (!force_res && use_custom_res && globals->has("display/test_width") && globals->has("display/test_height")) {
		int tw = globals->get("display/test_width");
		int th = globals->get("display/test_height");
		if (tw>0 && th>0) {
			video_mode.width=tw;
			video_mode.height=th;
		}
	}

	GLOBAL_DEF("display/width",video_mode.width);
	GLOBAL_DEF("display/height",video_mode.height);
	GLOBAL_DEF("display/allow_hidpi",false);
	GLOBAL_DEF("display/fullscreen",video_mode.fullscreen);
	GLOBAL_DEF("display/resizable",video_mode.resizable);
	GLOBAL_DEF("display/borderless_window", video_mode.borderless_window);
	OS::get_singleton()->set_video_mode(video_mode);

	bool use_vsync = GLOBAL_DEF("display/use_vsync", true);
	OS::get_singleton()->set_use_vsync(use_vsync);

	GLOBAL_DEF("display/test_width",0);
	GLOBAL_DEF("display/test_height",0);
	{
		String orientation = GLOBAL_DEF("display/orientation","landscape");

		if (orientation=="portrait")
			OS::get_singleton()->set_screen_orientation(OS::SCREEN_PORTRAIT);
		else if (orientation=="reverse_landscape")
			OS::get_singleton()->set_screen_orientation(OS::SCREEN_REVERSE_LANDSCAPE);
		else if (orientation=="reverse_portrait")
			OS::get_singleton()->set_screen_orientation(OS::SCREEN_REVERSE_PORTRAIT);
		else if (orientation=="sensor_landscape")
			OS::get_singleton()->set_screen_orientation(OS::SCREEN_SENSOR_LANDSCAPE);
		else if (orientation=="sensor_portrait")
			OS::get_singleton()->set_screen_orientation(OS::SCREEN_SENSOR_PORTRAIT);
		else if (orientation=="sensor")
			OS::get_singleton()->set_screen_orientation(OS::SCREEN_SENSOR);
		else
			OS::get_singleton()->set_screen_orientation(OS::SCREEN_LANDSCAPE);
	}


	OS::get_singleton()->set_iterations_per_second(GLOBAL_DEF("physics/fixed_fps",60));
	OS::get_singleton()->set_target_fps(GLOBAL_DEF("debug/force_fps",0));

	int frame_delay=0;
	if (frame_delay==0) {
		frame_delay=GLOBAL_DEF("application/frame_delay_msec",0);
	}
	OS::get_singleton()->set_frame_delay(frame_delay);



	PathRemap::get_singleton()->load_remaps();

	Image icon(GLOBAL_DEF("application/icon", ""));
	OS::get_singleton()->set_icon(icon);

	VisualServer::get_singleton()->set_default_clear_color(GLOBAL_DEF("render/default_clear_color",Color(0.3,0.3,0.3)));

	GLOBAL_DEF("application/icon",String());
	Globals::get_singleton()->set_custom_property_info("application/icon",PropertyInfo(Variant::STRING,"application/icon",PROPERTY_HINT_FILE,"*.png,*.webp"));

	if (bool(GLOBAL_DEF("display/emulate_touchscreen",false))) {
		if (!OS::get_singleton()->has_touchscreen_ui_hint() && Input::get_singleton()) {
			//only if no touchscreen ui hint, set emulation
			InputDefault *id = Input::get_singleton()->cast_to<InputDefault>();
			if (id)
				id->set_emulate_touch(true);
		}
	}

	GLOBAL_DEF("display/custom_mouse_cursor",String());
	GLOBAL_DEF("display/custom_mouse_cursor_hotspot",Vector2());
	Globals::get_singleton()->set_custom_property_info("display/custom_mouse_cursor",PropertyInfo(Variant::STRING,"display/custom_mouse_cursor",PROPERTY_HINT_FILE,"*.png,*.webp"));

	if (String(Globals::get_singleton()->get("display/custom_mouse_cursor"))!=String()) {

		//print_line("use custom cursor");
		Ref<Texture> cursor=ResourceLoader::load(Globals::get_singleton()->get("display/custom_mouse_cursor"));
		if (cursor.is_valid()) {
			//	print_line("loaded ok");
			Vector2 hotspot = Globals::get_singleton()->get("display/custom_mouse_cursor_hotspot");
			Input::get_singleton()->set_custom_mouse_cursor(cursor,hotspot);
		}
	}

	ScriptServer::init_languages();
	TranslationServer::get_singleton()->clear();
	TranslationServer::get_singleton()->load_translations();

	return OK;
}
