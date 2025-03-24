#pragma once
static const ComponentInfo EntityMetaData = 
 	{ "Entity", Components::Entity::mask, 
 		{
		}
 	};
static const ComponentInfo NameMetaData = 
 	{ "Name", Components::Name::mask, 
 		{
		}
 	};
static const ComponentInfo ShaderMetaData = 
 	{ "Shader", Components::Shader::mask, 
 		{
			{ "id", PropertyTypes::_assetID, 1, offsetof(Components::Shader, id) },
		}
 	};
static const ComponentInfo MeshMetaData = 
 	{ "Mesh", Components::Mesh::mask, 
 		{
			{ "id", PropertyTypes::_assetID, 1, offsetof(Components::Mesh, id) },
		}
 	};
static const ComponentInfo TextureMetaData = 
 	{ "Texture", Components::Texture::mask, 
 		{
			{ "id", PropertyTypes::_assetID, 1, offsetof(Components::Texture, id) },
		}
 	};
static const ComponentInfo MaterialMetaData = 
 	{ "Material", Components::Material::mask, 
 		{
			{ "prameters", PropertyTypes::_float, 15, offsetof(Components::Material, prameters) },
		}
 	};
static const ComponentInfo TransformMetaData = 
 	{ "Transform", Components::Transform::mask, 
 		{
			{ "position", PropertyTypes::_float3, 1, offsetof(Components::Transform, position) },
			{ "rotation", PropertyTypes::_quaternion, 1, offsetof(Components::Transform, rotation) },
			{ "scale", PropertyTypes::_float3, 1, offsetof(Components::Transform, scale) },
		}
 	};
static const ComponentInfo WorldMatrixMetaData = 
 	{ "WorldMatrix", Components::WorldMatrix::mask, 
 		{
			{ "matrix", PropertyTypes::_float4x4, 1, offsetof(Components::WorldMatrix, matrix) },
		}
 	};
static const ComponentInfo InstanceMetaData = 
 	{ "Instance", Components::Instance::mask, 
 		{
		}
 	};
static const ComponentInfo ParentMetaData = 
 	{ "Parent", Components::Parent::mask, 
 		{
		}
 	};
static const ComponentInfo LightMetaData = 
 	{ "Light", Components::Light::mask, 
 		{
			{ "matrix", PropertyTypes::_float4x4, 1, offsetof(Components::Light, matrix) },
		}
 	};
static const ComponentInfo CameraMetaData = 
 	{ "Camera", Components::Camera::mask, 
 		{
			{ "fovY", PropertyTypes::_float, 1, offsetof(Components::Camera, fovY) },
			{ "nearClip", PropertyTypes::_float, 1, offsetof(Components::Camera, nearClip) },
			{ "farClip", PropertyTypes::_float, 1, offsetof(Components::Camera, farClip) },
		}
 	};
ComponentInfo knownComponents[] = 
{
	EntityMetaData,
	NameMetaData,
	ShaderMetaData,
	MeshMetaData,
	TextureMetaData,
	MaterialMetaData,
	TransformMetaData,
	WorldMatrixMetaData,
	InstanceMetaData,
	ParentMetaData,
	LightMetaData,
	CameraMetaData,
};
