// vim: set commentstring=//\ %s:
#version 300

@ctype mat4 glm::mat4
@ctype vec2 glm::vec2
@ctype vec3 glm::vec3
@ctype vec4 glm::vec4

@vs mmd_vs
in vec3 in_Pos;
in vec3 in_Nor;
in vec2 in_UV;

out vec3 vs_Pos;
out vec3 vs_Nor;
out vec2 vs_UV;

uniform u_mmd_vs {
    mat4 u_WV;
    mat4 u_WVP;
};

void main()
{
    gl_Position = u_WVP * vec4(in_Pos, 1.0);

    vs_Pos = (u_WV * vec4(in_Pos, 1.0)).xyz;
    vs_Nor = mat3(u_WV) * in_Nor;
    vs_UV = in_UV;
    // vs_UV = vec2(in_UV.x, 1.0 - in_UV.y);
}
@end

@fs mmd_fs
in vec3 vs_Pos;
in vec3 vs_Nor;
in vec2 vs_UV;

out vec4 out_Color;

uniform u_mmd_fs {
    float u_Alpha;
    vec3 u_Diffuse;
    vec3 u_Ambient;
    vec3 u_Specular;
    float u_SpecularPower;
    vec3 u_LightColor;
    vec3 u_LightDir;

    int u_TexMode;
    vec4 u_TexMulFactor;
    vec4 u_TexAddFactor;

    int u_ToonTexMode;
    vec4 u_ToonTexMulFactor;
    vec4 u_ToonTexAddFactor;

    int u_SphereTexMode;
    vec4 u_SphereTexMulFactor;
    vec4 u_SphereTexAddFactor;
};

uniform sampler2D u_Tex_mmd;
uniform sampler2D u_ToonTex;
uniform sampler2D u_SphereTex;

vec3 ComputeTexMulFactor(vec3 texColor, vec4 factor)
{
    vec3 ret = texColor * factor.rgb;
    return mix(vec3(1.0, 1.0, 1.0), ret, factor.a);
}

vec3 ComputeTexAddFactor(vec3 texColor, vec4 factor)
{
    vec3 ret = texColor + (texColor - vec3(1.0)) * factor.a ;
    ret = clamp(ret, vec3(0.0), vec3(1.0))+ factor.rgb;
    return ret;
}

void main()
{
    vec3 eyeDir = normalize(vs_Pos);
    vec3 lightDir = normalize(-u_LightDir);
    vec3 nor = normalize(vs_Nor);
    float ln = dot(nor, lightDir);
    ln = clamp(ln + 0.5, 0.0, 1.0);
    vec3 color = vec3(0.0, 0.0, 0.0);
    float alpha = u_Alpha;
    vec3 diffuseColor = u_Diffuse * u_LightColor;
    color = diffuseColor;
    color += u_Ambient;
    color = clamp(color, 0.0, 1.0);

    if (u_TexMode != 0)
    {
        vec4 texColor = texture(u_Tex_mmd, vs_UV);
        texColor.rgb = ComputeTexMulFactor(texColor.rgb, u_TexMulFactor);
        texColor.rgb = ComputeTexAddFactor(texColor.rgb, u_TexAddFactor);
        color *= texColor.rgb;
        if (u_TexMode == 2)
        {
            alpha *= texColor.a;
        }
    }

    if (alpha == 0.0)
    {
        discard;
    }

    if (u_SphereTexMode != 0)
    {
        vec2 spUV = vec2(0.0);
        spUV.x = nor.x * 0.5 + 0.5;
        spUV.y = 1.0 - (nor.y * 0.5 + 0.5);
        vec3 spColor = texture(u_SphereTex, spUV).rgb;
        spColor = ComputeTexMulFactor(spColor, u_SphereTexMulFactor);
        spColor = ComputeTexAddFactor(spColor, u_SphereTexAddFactor);
        if (u_SphereTexMode == 1)
        {
            color *= spColor;
        }
        else if (u_SphereTexMode == 2)
        {
            color += spColor;
        }
    }

    if (u_ToonTexMode != 0)
    {
        // vec3 toonColor = texture(u_ToonTex, vec2(0.0, 1.0 - ln)).rgb;
        vec3 toonColor = texture(u_ToonTex, vec2(0.0, ln)).rgb;
        toonColor = ComputeTexMulFactor(toonColor, u_ToonTexMulFactor);
        toonColor = ComputeTexAddFactor(toonColor, u_ToonTexAddFactor);
        color *= toonColor;
    }

    vec3 specular = vec3(0.0);
    if (u_SpecularPower > 0)
    {
        vec3 halfVec = normalize(eyeDir + lightDir);
        vec3 specularColor = u_Specular * u_LightColor;
        specular += pow(max(0.0, dot(halfVec, nor)), u_SpecularPower) * specularColor;
    }

    color += specular;

    out_Color = vec4(color, alpha);
}
@end

@vs quad_vs
out vec2 vs_UV;

void main()
{
    const vec2 positions[4] = vec2[4](
        vec2(-1.0, 1.0f),
        vec2(-1.0, -1.0f),
        vec2(1.0, 1.0f),
        vec2(1.0, -1.0f)
    );

    const vec2 uvs[4] = vec2[4](
        vec2(0.0, 1.0),
        vec2(0.0, 0.0),
        vec2(1.0, 1.0),
        vec2(1.0, 0.0)
    );

    gl_Position = vec4(positions[gl_VertexIndex % 4], 0.0, 1.0);
    vs_UV = uvs[gl_VertexIndex % 4];
}
@end

@fs copy_fs
in vec2 vs_UV;

uniform sampler2D u_Tex_copy;

out vec4 fs_Color;

void main()
{
    fs_Color = texture(u_Tex_copy, vs_UV);
}
@end

// fragment shader for windows
// TODO: Rename to copy_fs_windows
@fs copy_transparent_window_fs
in vec2 vs_UV;

uniform sampler2D u_Tex_copy_transparent_window;

out vec4 fs_Color;

void main()
{
    vec4 texColor = texture(u_Tex_copy_transparent_window, vs_UV);
    float invAlpha = 1.0 - texColor.a;
    if (invAlpha == 0.0)
    {
        fs_Color = vec4(1.0, 0.0, 1.0, 0.0);
    }
    else
    {
        fs_Color.rgb = texColor.rgb;
        fs_Color.a = invAlpha;
    }
}
@end

@program mmd mmd_vs mmd_fs
@program copy quad_vs copy_fs
@program copy_transparent_window quad_vs copy_transparent_window_fs
