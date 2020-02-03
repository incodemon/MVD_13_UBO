//
//  Copyright 2018 Alun Evans. All rights reserved.
//
#include "GraphicsSystem.h"
#include "Parsers.h"
#include "extern.h"
#include <algorithm>

//destructor
GraphicsSystem::~GraphicsSystem() {
	//delete shader pointers
	for (auto shader_pair : shaders_) {
		if (shader_pair.second)
			delete shader_pair.second;
	}
}

//set initial state of graphics system
void GraphicsSystem::init(int window_width, int window_height, std::string assets_folder) {

	screen_background_color = lm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    updateMainViewport(window_width, window_height);
    
    //enable culling and depth test
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL); //for cubemap optimization
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    
    //enable seamless cubemap sampling
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    
    assets_folder_ = assets_folder;
    
}

//called after loading everything
void GraphicsSystem::lateInit() {
    sortMeshes_();
}

void GraphicsSystem::update(float dt) {
    
	bindAndClearScreen_();
    
	resetShaderAndMaterial_();
    
	updateAllCameras_();
    
    for (auto &mesh : ECS.getAllComponents<Mesh>()) {
        renderMeshComponent_(mesh);
    }
    
    renderEnvironment_();
    
}

//renders a given mesh component
void GraphicsSystem::renderMeshComponent_(Mesh& comp) {
	
	//change shader and mesh if required
	checkShaderAndMaterial(comp);

	//get components and geom
	Transform& transform = ECS.getComponentFromEntity<Transform>(comp.owner);
	Camera& cam = ECS.getComponentInArray<Camera>(ECS.main_camera);
	Geometry& geom = geometries_[comp.geometry];

	//create mvp
	lm::mat4 model_matrix = transform.getGlobalMatrix(ECS.getAllComponents<Transform>());
	lm::mat4 mvp_matrix = cam.view_projection * model_matrix;

	//view frustum culling
	if (!BBInFrustum_(geom.aabb, mvp_matrix)) {
		return;
	}

	//normal matrix
	lm::mat4 normal_matrix = model_matrix;
	normal_matrix.inverse();
	normal_matrix.transpose();

	//transform uniforms
	shader_->setUniform(U_MVP, mvp_matrix);
	shader_->setUniform(U_MODEL, model_matrix);
	shader_->setUniform(U_NORMAL_MATRIX, normal_matrix);
	shader_->setUniform(U_CAM_POS, cam.position);

	//draw
	geom.render();

}

//render the skybox as a cubemap
void GraphicsSystem::renderEnvironment_() {
    
	//render cubemap only if we have both a shader, texture, and geometry
	if (!environment_program_ || !environment_tex_ || cube_map_geom_ < 0) return;

    //set shader
    useShader(environment_program_);
    
    //get camera
    Camera& cam = ECS.getComponentInArray<Camera>(ECS.main_camera);
        
    //view projection matrix, zeroing out
    lm::mat4 view_matrix = cam.view_matrix;
    view_matrix.m[12] = view_matrix.m[13] = view_matrix.m[14] = 0; view_matrix.m[15] = 1;
    lm::mat4 vp_matrix = cam.projection_matrix * view_matrix;

    //set vp uniform and texture
    shader_->setUniform(U_VP, vp_matrix);
    
    //bind texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, environment_tex_);

	//no need to set sampler id, as it will default to 0
    
    // disable depth test, cull front faces (to draw inside of mesh)
    glDepthMask(false);
    glCullFace(GL_FRONT);
    

	geometries_[cube_map_geom_].render();
    
    // reset depth test and culling
    glDepthMask(true);
    glCullFace(GL_BACK);
    
}

//checks to see if current shader and material are
//the ones need for mesh passed as parameter
//if not, change them
void GraphicsSystem::checkShaderAndMaterial(Mesh& mesh) {
    //get shader id from material. if same, don't change
    if (!shader_ || shader_->program != materials_[mesh.material].shader_id) {
		useShader(materials_[mesh.material].shader_id);
    }
    //set material uniforms if required
    if (current_material_ != mesh.material) {
        current_material_ = mesh.material;
        setMaterialUniforms();
    }
}

//sets uniforms for current material and current shader
void GraphicsSystem::setMaterialUniforms() {
    Material& mat = materials_[current_material_];
    
	//temporary stuff for UBO_test

	//GLint u_color_a = glGetUniformLocation(shader_->program, "u_test_struct.color_a"); 
	//if (u_color_a != -1) glUniform3f(u_color_a, 1.0, 0.0, 1.0);

	//GLint u_color_b = glGetUniformLocation(shader_->program, "u_test_struct.color_b");
	//if (u_color_b != -1) glUniform3f(u_color_b, 0.0, 1.0, 0.0);

	//declare size
	GLsizeiptr size_test_ubo = 16 + 16; //2 * vec4 (declared in shader)
	
	//create our UBO
	GLuint uboTest;
	glGenBuffers(1, &uboTest);
	glBindBuffer(GL_UNIFORM_BUFFER,uboTest);
	glBufferData(GL_UNIFORM_BUFFER, size_test_ubo,NULL,GL_STATIC_DRAW);

	//fill our ubo
	//declare a "running counter" of where are in our memory
	GLintptr offset = 0;

	GLfloat color_a_data[4] = { 1.0f , 0.0f , 1.0f , 0.0f }; //the color green
	glBufferSubData(GL_UNIFORM_BUFFER,
					offset, //where to start writing memory in UBO
					16, //Size of data
					color_a_data); //array pointer

	offset += 16;

	GLfloat color_b_data[4] = { 0.0f , 1.0f , 0.0f , 0.0f }; //the color purple
	glBufferSubData(GL_UNIFORM_BUFFER,
		offset, //where to start writing memory in UBO
		16, //Size of data
		color_b_data); //array pointer
	offset += 16; //I kow that i've never will use it again
	GLuint TEST_BINDING_POINT = 0;
	glBindBufferRange(GL_UNIFORM_BUFFER,  //the type of buffer
						TEST_BINDING_POINT, //the index of binding point = 0
						uboTest, //the buffer we want to bind
						0,	 //starting offset
						size_test_ubo); //size of whole buffer

	GLuint UBO_test_handle = glGetUniformBlockIndex(shader_->program, "UBO_test");
	if (UBO_test_handle != -1) glUniformBlockBinding(shader_->program,UBO_test_handle,TEST_BINDING_POINT);

    //material uniforms
	shader_->setUniform(U_AMBIENT, mat.ambient);
	shader_->setUniform(U_DIFFUSE, mat.diffuse);
	shader_->setUniform(U_SPECULAR, mat.specular);
	shader_->setUniform(U_SPECULAR_GLOSS, mat.specular_gloss);
    
    //texture uniforms
    if (mat.diffuse_map != -1){
        shader_->setUniform(U_USE_DIFFUSE_MAP, 1);
        shader_->setTexture(U_DIFFUSE_MAP, mat.diffuse_map, 0);
    }

    //reflection
    if (mat.cube_map) {
        shader_->setUniform(U_USE_REFLECTION_MAP, 1);
        shader_->setTextureCube(U_SKYBOX, mat.cube_map, 1);
        
    }
    
    //light uniforms
    const std::vector<Light>& lights = ECS.getAllComponents<Light>();  // get number of lights in scene from ECM
    
    GLint u_num_lights = glGetUniformLocation(shader_->program, "u_num_lights"); //get/set uniform in shader
    if (u_num_lights != -1) glUniform1i(u_num_lights, (int)lights.size());
    
    //for each light
    for (size_t i = 0; i < lights.size(); i++) {
        //get light transform
        Transform& light_transform = ECS.getComponentFromEntity<Transform>(lights[i].owner);
        
        //position
        std::string light_position_name = "lights[" + std::to_string(i) + "].position"; // create dynamic uniform name
        GLint u_light_pos = glGetUniformLocation(shader_->program, light_position_name.c_str()); //find it
        if (u_light_pos != -1) glUniform3fv(u_light_pos, 1, light_transform.position().value_); // light position
        
        //color
        std::string light_color_name = "lights[" + std::to_string(i) + "].color";
        GLint u_light_col = glGetUniformLocation(shader_->program, light_color_name.c_str());
        if (u_light_col != -1) glUniform3fv(u_light_col, 1, lights[i].color.value_);
        
        //direction
        std::string light_direction_name = "lights[" + std::to_string(i) + "].direction";
        GLint u_light_dir = glGetUniformLocation(shader_->program, light_direction_name.c_str());
        glUniform3fv(u_light_dir, 1, lights[i].direction.value_);
        
        //type
        std::string light_type_name = "lights[" + std::to_string(i) + "].type";
        GLint u_light_type = glGetUniformLocation(shader_->program, light_type_name.c_str());
        glUniform1i(u_light_type, lights[i].type);
        
        //linear att
        std::string light_latt_name = "lights[" + std::to_string(i) + "].linear_att";
        GLint u_light_latt = glGetUniformLocation(shader_->program, light_latt_name.c_str());
        glUniform1f(u_light_latt, lights[i].linear_att);
        
        //quadratic att
        std::string light_qatt_name = "lights[" + std::to_string(i) + "].quadratic_att";
        GLint u_light_qatt = glGetUniformLocation(shader_->program, light_qatt_name.c_str());
        glUniform1f(u_light_qatt, lights[i].quadratic_att);

		//spotcone cutoff
		std::string light_innercone_name = "lights[" + std::to_string(i) + "].spot_inner_cosine";
		GLint u_light_innercone = glGetUniformLocation(shader_->program, light_innercone_name.c_str());
		glUniform1f(u_light_innercone, cos (lights[i].spot_inner*DEG2RAD / 2));

		//spotcone cutoff outer
		std::string light_outercone_name = "lights[" + std::to_string(i) + "].spot_outer_cosine";
		GLint u_light_outercone = glGetUniformLocation(shader_->program, light_outercone_name.c_str());
		glUniform1f(u_light_outercone, cos(lights[i].spot_outer *DEG2RAD / 2));

    }
}

//This function executes two sorts:
// i) sorts materials array by shader_id
// ii) sorts Mesh components by material id
//the result is that the mesh component array is
//ordered by both shader and material
void GraphicsSystem::sortMeshes_() {

	//sort materials by shader id
	//first we store the old index of each material in materials_ array
	for (size_t i = 0; i < materials_.size(); i++)
		materials_[i].index = (int)i; // 'index' is a new property of Material

	//second, we sort materials by shader_id
	std::sort(materials_.begin(), materials_.end(), [](const Material& a, const Material& b) {
		return a.shader_id < b.shader_id;
	});

	//now we map old indices to new indices
	std::map<int, int> old_new;
	for (size_t i = 0; i < materials_.size(); i++) {
		old_new[materials_[i].index] = (int)i;
	}

	//now we swap index of materials in all meshes
	auto& meshes = ECS.getAllComponents<Mesh>();
	for (auto& mesh : meshes) {
		int old_index = mesh.material;
		int new_index = old_new[old_index];
		mesh.material = new_index;
	}

	//store old mesh indices
	for (size_t i = 0; i < meshes.size(); i++)
		meshes[i].index = (int)i;

	//short meshes by material id
	std::sort(meshes.begin(), meshes.end(), [](const Mesh& a, const Mesh& b) {
		return a.material < b.material;
	});

	//clear map and refill with mesh index map
	old_new.clear();
	for (size_t i = 0; i < meshes.size(); i++) {
		old_new[meshes[i].index] = (int)i;
	}

	//update all entities with new mesh id
	auto& all_entities = ECS.entities;
	for (auto& ent : ECS.entities) {
		int old_index = ent.components[type2int<Mesh>::result];
		int new_index = old_new[old_index];
		ent.components[type2int<Mesh>::result] = new_index;
	}
}

//reset shader and material
void GraphicsSystem::resetShaderAndMaterial_() {
	
	useShader((GLuint)0);
	current_material_ = -1;
}

//update cameras
void GraphicsSystem::updateAllCameras_() {

	auto& cameras = ECS.getAllComponents<Camera>();
	for (auto &cam : cameras) cam.update();
}

void GraphicsSystem::bindAndClearScreen_() {
	glViewport(0, 0, viewport_width_, viewport_height_);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glClearColor(screen_background_color.x, screen_background_color.y, screen_background_color.z, screen_background_color.w);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

//change shader only if need to - note shader object must be in shaders_ map
//s - pointer to a shader object
void GraphicsSystem::useShader(Shader* s) {
	if (!s) {
		glUseProgram(0);
		shader_ = nullptr;
	}
	else if (!shader_ || shader_ != s) {
		glUseProgram(s->program);
		shader_ = s;
	}
}

//change shader only if need to - note shader object must be in shaders_ map
//p - GL id of shader
void GraphicsSystem::useShader(GLuint p) {
	if (!p) {
		glUseProgram(0);
		shader_ = nullptr;
	}
	else if (!shader_ || shader_->program != p) {
		glUseProgram(p);
		shader_ = shaders_[p];
	}
}

//sets internal variables
void GraphicsSystem::setEnvironment(GLuint tex_id, int geom_id, GLuint program) {

	//set cubemap geometry
	cube_map_geom_ = geom_id;

	//set cube faces
	environment_tex_ = tex_id;

	//set shader program
	environment_program_ = program;
}

//
////********************************************
//// Adding and creating functions
////********************************************

//loads a shader, stores it in a map where key is it's program id, and returns a pointer to shader
//-vs: either the path to the vertex shader, or the vertex shader string
//-fs: either the path to the fragment shader, or the fragment shader string
//-compile_direct: if false, assume other two parameters are paths, if true, assume they are shader strings
Shader* GraphicsSystem::loadShader(std::string vs, std::string fs, bool compile_direct) {
	Shader* new_shader;
	if (compile_direct) {
		new_shader = new Shader();
		new_shader->compileFromStrings(vs, fs);
	}
	else {
		new_shader = new Shader(vs, fs);
	}
	shaders_[new_shader->program] = new_shader;
	return new_shader;
}

//create a new material and return pointer to it
int GraphicsSystem::createMaterial() {
    materials_.emplace_back();
    return (int)materials_.size() - 1;
}


//create geometry from
//returns index in geometry array with stored geometry data
int GraphicsSystem::createGeometryFromFile(std::string filename) {
    
    std::vector<GLfloat> vertices, uvs, normals;
    std::vector<GLuint> indices;
    //check for supported format
    std::string ext = filename.substr(filename.size() - 4, 4);
    if (ext == ".obj" || ext == ".OBJ")
    {
        //fill it with data from object
        if (Parsers::parseOBJ(filename, vertices, uvs, normals, indices)) {
            
            //generate the OpenGL buffers and create geometry
			Geometry new_geom(vertices, uvs, normals, indices);
            geometries_.emplace_back(new_geom);

            return (int)geometries_.size() - 1;
        }
        else {
            std::cerr << "ERROR: Could not parse mesh file" << std::endl;
            return -1;
        }
    }
    else {
        std::cerr << "ERROR: Unsupported mesh format when creating geometry" << std::endl;
        return -1;
    }
    
}

// Given an array of floats (in sets of three, representing vertices) calculates and
// sets the AABB of a geometry
void GraphicsSystem::setGeometryAABB_(Geometry& geom, std::vector<GLfloat>& vertices) {
	//set very max and very min
	float big = 1000000.0f;
	float small = -1000000.0f;
	lm::vec3 min(big, big, big);
	lm::vec3 max(small, small, small);

	//for all verts, find max and min
	for (size_t i = 0; i < vertices.size(); i += 3) {
		float x = vertices[i];
		float y = vertices[i + 1];
		float z = vertices[i + 2];

		if (x < min.x) min.x = x;
		if (y < min.y) min.y = y;
		if (z < min.z) min.z = z;

		if (x > max.x) max.x = x;
		if (y > max.y) max.y = y;
		if (z > max.z) max.z = z;
	}
	//set center and halfwidth based on max and min
	geom.aabb.center = lm::vec3(
		(min.x + max.x) / 2,
		(min.y + max.y) / 2,
		(min.z + max.z) / 2);
	geom.aabb.half_width = lm::vec3(
		max.x - geom.aabb.center.x,
		max.y - geom.aabb.center.y,
		max.z - geom.aabb.center.z);
}

//relcalculates AABB from OOB
AABB GraphicsSystem::transformAABB_(const AABB& aabb, const lm::mat4& transform) {
	//get aabb min and max
	lm::vec3 aabb_min(
		aabb.center.x - aabb.half_width.x,
		aabb.center.y - aabb.half_width.y,
		aabb.center.z - aabb.half_width.z);
	lm::vec3 aabb_max(
		aabb.center.x + aabb.half_width.x,
		aabb.center.y + aabb.half_width.y,
		aabb.center.z + aabb.half_width.z);

	//transform min and max
	lm::vec3 min_t = transform * aabb_min;
	lm::vec3 max_t = transform * aabb_max;

	//create array of coords for easier parsing
	float vertices[6] = { min_t.x, min_t.y, min_t.z, max_t.x, max_t.y, max_t.z };

	//set very max and very min
	float big = 1000000.0f;
	float small = -1000000.0f;
	lm::vec3 min(big, big, big);
	lm::vec3 max(small, small, small);

	//calculate new aabb based on transformed verts
	for (int i = 0; i < 6; i += 3) {
		float x = vertices[i];
		float y = vertices[i + 1];
		float z = vertices[i + 2];

		if (x < min.x) min.x = x;
		if (y < min.y) min.y = y;
		if (z < min.z) min.z = z;

		if (x > max.x) max.x = x;
		if (y > max.y) max.y = y;
		if (z > max.z) max.z = z;
	}

	AABB new_aabb;
	//set new center and halfwidth based on max and min
	new_aabb.center = lm::vec3(
		(min.x + max.x) / 2,
		(min.y + max.y) / 2,
		(min.z + max.z) / 2);
	new_aabb.half_width = lm::vec3(
		max.x - new_aabb.center.x,
		max.y - new_aabb.center.y,
		max.z - new_aabb.center.z);

	return new_aabb;
}


//tests whether AABB or OOB is inside frustum or not, based on view_projection matrix
bool GraphicsSystem::AABBInFrustum_(const AABB& aabb, const lm::mat4& to_clip) {
	lm::vec4 points[8];
	points[0] = lm::vec4(aabb.center.x - aabb.half_width.x, aabb.center.y - aabb.half_width.y, aabb.center.z - aabb.half_width.z, 1.0);
	points[1] = lm::vec4(aabb.center.x - aabb.half_width.x, aabb.center.y - aabb.half_width.y, aabb.center.z + aabb.half_width.z, 1.0);
	points[2] = lm::vec4(aabb.center.x - aabb.half_width.x, aabb.center.y + aabb.half_width.y, aabb.center.z - aabb.half_width.z, 1.0);
	points[3] = lm::vec4(aabb.center.x - aabb.half_width.x, aabb.center.y + aabb.half_width.y, aabb.center.z + aabb.half_width.z, 1.0);
	points[4] = lm::vec4(aabb.center.x + aabb.half_width.x, aabb.center.y - aabb.half_width.y, aabb.center.z - aabb.half_width.z, 1.0);
	points[5] = lm::vec4(aabb.center.x + aabb.half_width.x, aabb.center.y - aabb.half_width.y, aabb.center.z + aabb.half_width.z, 1.0);
	points[6] = lm::vec4(aabb.center.x + aabb.half_width.x, aabb.center.y + aabb.half_width.y, aabb.center.z - aabb.half_width.z, 1.0);
	points[7] = lm::vec4(aabb.center.x + aabb.half_width.x, aabb.center.y + aabb.half_width.y, aabb.center.z + aabb.half_width.z, 1.0);

	//transform to clip space
	lm::vec4 clip_points[8];
	for (int i = 0; i < 8; i++) {
		clip_points[i] = to_clip * points[i];
	}

	//now test clip points against each plane. If all clip points are outside plane we return false
	//left plane
	int in = 0;
	for (int i = 0; i < 8; i++) {
		if (-clip_points[i].w < clip_points[i].x) in++;
	}
	if (!in) return false;

	//right plane
	in = 0;
	for (int i = 0; i < 8; i++) {
		if (clip_points[i].x < clip_points[i].w) in++;
	}
	if (!in) return false;

	//bottom plane
	in = 0;
	for (int i = 0; i < 8; i++) {
		if (-clip_points[i].w < clip_points[i].y) in++;
	}
	if (!in) return false;

	//top plane
	in = 0;
	for (int i = 0; i < 8; i++) {
		if (clip_points[i].y < clip_points[i].w) in++;
	}
	if (!in) return false;

	//near plane
	in = 0;
	for (int i = 0; i < 8; i++) {
		if (-clip_points[i].z < clip_points[i].z) in++;
	}
	if (!in) return false;

	//far plane
	in = 0;
	for (int i = 0; i < 8; i++) {
		if (clip_points[i].z < clip_points[i].w) in++;
	}
	if (!in) return false;

	return true;
}

//tests whether Bounding box is inside frustum or not, based on model_view_projection matrix
bool GraphicsSystem::BBInFrustum_(const AABB& aabb, const lm::mat4& mvp) {
    //each corner point of box gets transformed into clip space, to give point PC, in HOMOGENOUS coords
    //point is inside clip space iff
    //-PC.w < PC.xyz < PC.w
    //so we first take each corner of AABB (note, using vec4 because of homogenous coords) and multiply
    //by matrix to clip space. Then we test each point against the 6 planes e.g. PC is on the 'right' side
    //of the left plane iff -PC.w < PC.x; and is on 'left' side of right plane is PC.x < PC.w etc.
    //For more info see:
    //http://www.lighthouse3d.com/tutorials/view-frustum-culling/clip-space-approach-extracting-the-planes/
    
    
    //the eight points of the box corners are calculated using center and +/- halfwith:
    //- - -
    //- - +
    //- + -
    //- + +
    //+ - -
    //+ - +
    //+ + -
    //+ + +
	lm::vec4 points[8];
	points[0] = lm::vec4(aabb.center.x - aabb.half_width.x, aabb.center.y - aabb.half_width.y, aabb.center.z - aabb.half_width.z, 1.0);
	points[1] = lm::vec4(aabb.center.x - aabb.half_width.x, aabb.center.y - aabb.half_width.y, aabb.center.z + aabb.half_width.z, 1.0);
	points[2] = lm::vec4(aabb.center.x - aabb.half_width.x, aabb.center.y + aabb.half_width.y, aabb.center.z - aabb.half_width.z, 1.0);
	points[3] = lm::vec4(aabb.center.x - aabb.half_width.x, aabb.center.y + aabb.half_width.y, aabb.center.z + aabb.half_width.z, 1.0);
	points[4] = lm::vec4(aabb.center.x + aabb.half_width.x, aabb.center.y - aabb.half_width.y, aabb.center.z - aabb.half_width.z, 1.0);
	points[5] = lm::vec4(aabb.center.x + aabb.half_width.x, aabb.center.y - aabb.half_width.y, aabb.center.z + aabb.half_width.z, 1.0);
	points[6] = lm::vec4(aabb.center.x + aabb.half_width.x, aabb.center.y + aabb.half_width.y, aabb.center.z - aabb.half_width.z, 1.0);
	points[7] = lm::vec4(aabb.center.x + aabb.half_width.x, aabb.center.y + aabb.half_width.y, aabb.center.z + aabb.half_width.z, 1.0);

	//transform to clip space
	lm::vec4 clip_points[8];
	for (int i = 0; i < 8; i++) {
		clip_points[i] = mvp * points[i];
	}

	//now test clip points against each plane. If all clip points are outside plane we return false
	//left plane
	int in = 0;
	for (int i = 0; i < 8; i++) {
		if (-clip_points[i].w < clip_points[i].x) in++;
	}
	if (!in) return false;

	//right plane
	in = 0;
	for (int i = 0; i < 8; i++) {
		if (clip_points[i].x < clip_points[i].w) in++;
	}
	if (!in) return false;

	//bottom plane
	in = 0;
	for (int i = 0; i < 8; i++) {
		if (-clip_points[i].w < clip_points[i].y) in++;
	}
	if (!in) return false;

	//top plane
	in = 0;
	for (int i = 0; i < 8; i++) {
		if (clip_points[i].y < clip_points[i].w) in++;
	}
	if (!in) return false;

	//near plane
	in = 0;
	for (int i = 0; i < 8; i++) {
		if (-clip_points[i].z < clip_points[i].z) in++;
	}
	if (!in) return false;

	//far plane
	in = 0;
	for (int i = 0; i < 8; i++) {
		if (clip_points[i].z < clip_points[i].w) in++;
	}
	if (!in) return false;

	return true;
}

//sets viewport of graphics system
void GraphicsSystem::updateMainViewport(int window_width, int window_height) {
    glViewport(0, 0, window_width, window_height);
    viewport_width_ = window_width;
    viewport_height_ = window_height;
}

//returns values of window width and height by reference
void GraphicsSystem::getMainViewport(int& width, int& height){
    width = viewport_width_; height = viewport_height_;
}

