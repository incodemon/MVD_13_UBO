#version 330

//varyings and out color
in vec3 v_normal;

out vec4 fragColor;

struct TestStruct {
	vec4 color_a;
	vec4 color_b;
};

layout (std140) uniform UBO_test{
	TestStruct u_test_struct;
};



void main(){

	vec3 N = normalize(v_normal);

	float col_f = sin(v_normal.x);
	
	vec3 final_color = mix(u_test_struct.color_a.xyz, u_test_struct.color_b.xyz, col_f);
	
	fragColor = vec4(final_color, 1.0);
}
