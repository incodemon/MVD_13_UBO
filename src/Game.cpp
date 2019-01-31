//
//  Game.cpp
//
//  Copyright � 2018 Alun Evans. All rights reserved.
//

#include "Game.h"
#include "Shader.h"
#include "extern.h"
#include "Parsers.h"

Game::Game() {

}

//Nothing here yet
void Game::init() {

	//******* INIT SYSTEMS *******

	//init systems except debug, which needs info about scene
	control_system_.init();
	graphics_system_.init(window_width_, window_height_, "data/assets/");
    script_system_.init(&control_system_);
	gui_system_.init(window_width_, window_height_);

    
	//camera
	createFreeCamera_();
    
	//******** TEST SPHERE  **********/

	//test sphere
	Shader* ubo_test_shader = graphics_system_.loadShader("data/shaders/ubo_test.vert", "data/shaders/ubo_test.frag");

	int sphere_entity = ECS.createEntity("sphere");
	ECS.getComponentFromEntity<Transform>(sphere_entity).translate(0.0f, 2.0f, 0.0f);
	Mesh& sphere_mesh = ECS.createComponentForEntity<Mesh>(sphere_entity);
	sphere_mesh.geometry = graphics_system_.createGeometryFromFile("data/assets/sphere.obj");
	sphere_mesh.material = graphics_system_.createMaterial();
	Material& sphere_mat = graphics_system_.getMaterial(sphere_mesh.material);
	sphere_mat.shader_id = ubo_test_shader->program;



    //******** FLOOR  **********/
    
    Shader* phong_shader = graphics_system_.loadShader("data/shaders/phong.vert", "data/shaders/phong.frag");
    
    //create a floor
    int floor_entity = ECS.createEntity("floor");
    ECS.getComponentFromEntity<Transform>(floor_entity).translate(0.0f, 0.0f, 0.0f);
    Mesh& floor_mesh = ECS.createComponentForEntity<Mesh>(floor_entity);
    floor_mesh.geometry = graphics_system_.createGeometryFromFile("data/assets/floor_40x40.obj");
    floor_mesh.material = graphics_system_.createMaterial();
    graphics_system_.getMaterial(floor_mesh.material).shader_id = phong_shader->program;
	Material& floor_mat = graphics_system_.getMaterial(floor_mesh.material);
	floor_mat.shader_id = phong_shader->program;
	floor_mat.diffuse_map = Parsers::parseTexture("data/assets/block_blue.tga");
    
	//******** LIGHTS  **********/
    
 //   //light 1 - directional
    int ent_light1 = ECS.createEntity("light 1");
    auto& light1 = ECS.createComponentForEntity<Light>(ent_light1);
    light1.color = lm::vec3(1.0f, 1.0f, 1.0f);
    light1.direction = lm::vec3(-1,-1,-1);
    light1.type = 0;

    //light 2 - point
    int ent_light2 = ECS.createEntity("light 2");
    ECS.getComponentFromEntity<Transform>(ent_light2).translate(-10.0f, 3.0f, -10.0f);
    auto& light2 = ECS.createComponentForEntity<Light>(ent_light2);
    light2.color = lm::vec3(1.0f, 0.0f, 0.0f);
    light2.linear_att = 0.022f;
    light2.quadratic_att = 0.0019f;
    light2.type = 1;

    //light 3 - spotlight
    int ent_light3 = ECS.createEntity("light 3");
    ECS.getComponentFromEntity<Transform>(ent_light3).translate(10.0f, 20.0f, -10.0f);
    auto& light3 = ECS.createComponentForEntity<Light>(ent_light3);
    light3.direction = lm::vec3(0.0, -1.0, 0.0);
    light3.color = lm::vec3(0.0f, 1.0f, 0.0f);
    light3.type = 2;
    light3.linear_att = 0.022f;
    light3.quadratic_att = 0.0019f;
    light3.spot_inner = 30.0f;
    light3.spot_outer = 40.0f;    

	

	//Parsers::parseJSONLevel("data/assets/lightcasters.json", graphics_system_, control_system_);
    
    
    
    //******* LATE INIT AFTER LOADING RESOURCES *******//
    graphics_system_.lateInit();
    script_system_.lateInit();
    debug_system_.lateInit();

}

//update each system in turn
void Game::update(float dt) {

	if (ECS.getAllComponents<Camera>().size() == 0) {print("There is no camera set!"); return;}

	//update input
	control_system_.update(dt);

	//collision
	collision_system_.update(dt);

	//scripts
	script_system_.update(dt);

	//render
	graphics_system_.update(dt);
    
	//gui
	gui_system_.update(dt);

	//debug
	debug_system_.update(dt);
   
}
//update game viewports
void Game::update_viewports(int window_width, int window_height) {
	window_width_ = window_width;
	window_height_ = window_height;

	auto& cameras = ECS.getAllComponents<Camera>();
	for (auto& cam : cameras) {
		cam.setPerspective(60.0f*DEG2RAD, (float)window_width_ / (float) window_height_, 0.01f, 10000.0f);
	}

	graphics_system_.updateMainViewport(window_width_, window_height_);
}


int Game::createFreeCamera_() {
	int ent_player = ECS.createEntity("PlayerFree");
	Camera& player_cam = ECS.createComponentForEntity<Camera>(ent_player);
	lm::vec3 the_position(0.0f, 5.0f, 15.0f);
	ECS.getComponentFromEntity<Transform>(ent_player).translate(the_position);
	player_cam.position = the_position;
	player_cam.forward = lm::vec3(0, -0.3f, -1.0f);
	player_cam.setPerspective(60.0f*DEG2RAD, (float)window_width_/(float)window_height_, 0.1f, 10000.0f);

	ECS.main_camera = ECS.getComponentID<Camera>(ent_player);

	control_system_.control_type = ControlTypeFree;

	return ent_player;
}

int Game::createPlayer_(float aspect, ControlSystem& sys) {
	int ent_player = ECS.createEntity("PlayerFPS");
	Camera& player_cam = ECS.createComponentForEntity<Camera>(ent_player);
	lm::vec3 the_position(0.0f, 3.0f, 5.0f);
	ECS.getComponentFromEntity<Transform>(ent_player).translate(the_position);
	player_cam.position = the_position;
	player_cam.forward = lm::vec3(0.0f, 0.0f, -1.0f);
	player_cam.setPerspective(60.0f*DEG2RAD, aspect, 0.01f, 10000.0f);

	//FPS colliders 
	//each collider ray entity is parented to the playerFPS entity
	int ent_down_ray = ECS.createEntity("Down Ray");
	Transform& down_ray_trans = ECS.getComponentFromEntity<Transform>(ent_down_ray);
	down_ray_trans.parent = ECS.getComponentID<Transform>(ent_player); //set parent as player entity *transform*!
	Collider& down_ray_collider = ECS.createComponentForEntity<Collider>(ent_down_ray);
	down_ray_collider.collider_type = ColliderTypeRay;
	down_ray_collider.direction = lm::vec3(0.0, -1.0, 0.0);
	down_ray_collider.max_distance = 100.0f;

	int ent_left_ray = ECS.createEntity("Left Ray");
	Transform& left_ray_trans = ECS.getComponentFromEntity<Transform>(ent_left_ray);
	left_ray_trans.parent = ECS.getComponentID<Transform>(ent_player); //set parent as player entity *transform*!
	Collider& left_ray_collider = ECS.createComponentForEntity<Collider>(ent_left_ray);
	left_ray_collider.collider_type = ColliderTypeRay;
	left_ray_collider.direction = lm::vec3(-1.0, 0.0, 0.0);
	left_ray_collider.max_distance = 1.0f;

	int ent_right_ray = ECS.createEntity("Right Ray");
	Transform& right_ray_trans = ECS.getComponentFromEntity<Transform>(ent_right_ray);
	right_ray_trans.parent = ECS.getComponentID<Transform>(ent_player); //set parent as player entity *transform*!
	Collider& right_ray_collider = ECS.createComponentForEntity<Collider>(ent_right_ray);
	right_ray_collider.collider_type = ColliderTypeRay;
	right_ray_collider.direction = lm::vec3(1.0, 0.0, 0.0);
	right_ray_collider.max_distance = 1.0f;

	int ent_forward_ray = ECS.createEntity("Forward Ray");
	Transform& forward_ray_trans = ECS.getComponentFromEntity<Transform>(ent_forward_ray);
	forward_ray_trans.parent = ECS.getComponentID<Transform>(ent_player); //set parent as player entity *transform*!
	Collider& forward_ray_collider = ECS.createComponentForEntity<Collider>(ent_forward_ray);
	forward_ray_collider.collider_type = ColliderTypeRay;
	forward_ray_collider.direction = lm::vec3(0.0, 0.0, -1.0);
	forward_ray_collider.max_distance = 1.0f;

	int ent_back_ray = ECS.createEntity("Back Ray");
	Transform& back_ray_trans = ECS.getComponentFromEntity<Transform>(ent_back_ray);
	back_ray_trans.parent = ECS.getComponentID<Transform>(ent_player); //set parent as player entity *transform*!
	Collider& back_ray_collider = ECS.createComponentForEntity<Collider>(ent_back_ray);
	back_ray_collider.collider_type = ColliderTypeRay;
	back_ray_collider.direction = lm::vec3(0.0, 0.0, 1.0);
	back_ray_collider.max_distance = 1.0f;

	//the control system stores the FPS colliders 
	sys.FPS_collider_down = ECS.getComponentID<Collider>(ent_down_ray);
	sys.FPS_collider_left = ECS.getComponentID<Collider>(ent_left_ray);
	sys.FPS_collider_right = ECS.getComponentID<Collider>(ent_right_ray);
	sys.FPS_collider_forward = ECS.getComponentID<Collider>(ent_forward_ray);
	sys.FPS_collider_back = ECS.getComponentID<Collider>(ent_back_ray);

	ECS.main_camera = ECS.getComponentID<Camera>(ent_player);

	sys.control_type = ControlTypeFPS;

	return ent_player;
}

