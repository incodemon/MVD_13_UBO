#version 330

layout(location = 0) in vec3 a_vertex;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec3 a_normal;

uniform mat4 u_mvp;
uniform mat4 u_model;
uniform mat4 u_normal_matrix;
uniform vec3 u_cam_pos; 

out vec2 v_uv;
out vec3 v_normal;
out vec3 v_vertex;

void main(){

	//rotate normal 
	v_normal = (u_normal_matrix * vec4(a_normal, 1.0)).xyz;

	gl_Position = u_mvp * vec4(a_vertex, 1.0);
}