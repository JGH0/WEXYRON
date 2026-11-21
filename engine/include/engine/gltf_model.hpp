/**
 * @file gltf_model.hpp
 * @brief glTF 2.0 runtime model: geometry, PBR materials, skinning and animation.
 *
 * The loader reads both `.gltf` (JSON + external/data-URI buffers) and `.glb`
 * (binary container with embedded buffers and images). It is deliberately
 * self-contained: no Assimp, no JSON library, no image library beyond the
 * stb_image already used by the engine.
 *
 * Supported glTF features
 * -----------------------
 *  - buffers/bufferViews/accessors, including sparse accessors and all
 *    component types (normalized integers are converted to floats)
 *  - meshes with POSITION, NORMAL, TEXCOORD_0, COLOR_0, JOINTS_0, WEIGHTS_0
 *    and indexed or non-indexed TRIANGLES primitives
 *  - node hierarchies with matrix or translation/rotation/scale transforms
 *  - materials: base colour factor/texture, metallic/roughness,
 *    emissive factor/texture, alpha mode (OPAQUE/MASK/BLEND), double-sided
 *  - skins with inverse bind matrices (GPU linear-blend skinning, 32 bones)
 *  - animations with TRANSLATION/ROTATION/SCALE channels and LINEAR, STEP and
 *    CUBICSPLINE interpolation; two clips can be cross-faded per instance
 *
 * Not supported (ignored gracefully): morph targets, KHR extensions that
 * change geometry, cameras and punctual lights, and primitives that are not
 * TRIANGLES.
 */

#ifndef CUBE_ENGINE_GLTF_MODEL_HPP
#define CUBE_ENGINE_GLTF_MODEL_HPP

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

class Shader;

/// One drawable primitive (a triangle list) with its own VAO/VBO/EBO.
struct GltfPrimitive {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
    int material = -1;
};

struct GltfMesh {
    std::vector<GltfPrimitive> primitives;
};

struct GltfMaterial {
    glm::vec4 baseColorFactor{1.0f};
    glm::vec3 emissiveFactor{0.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    GLuint baseColorTexture = 0;
    GLuint emissiveTexture = 0;
    bool hasBaseColorTexture = false;
    bool hasEmissiveTexture = false;
    int alphaMode = 0;  ///< 0 = OPAQUE, 1 = MASK, 2 = BLEND
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
};

struct GltfNode {
    std::string name;
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    int mesh = -1;
    int skin = -1;
    int parent = -1;
    std::vector<int> children;
};

struct GltfSkin {
    std::vector<int> joints;
    std::vector<glm::mat4> inverseBindMatrices;
};

struct GltfAnimationSampler {
    std::vector<float> times;
    /// VEC3 values are stored with w = 0, VEC4 (rotations) with xyzw. For
    /// CUBICSPLINE the accessor holds (inTangent, value, outTangent) per key.
    std::vector<glm::vec4> values;
    int interpolation = 0;  ///< 0 = LINEAR, 1 = STEP, 2 = CUBICSPLINE
};

struct GltfAnimationChannel {
    int node = -1;
    int path = 0;  ///< 0 = translation, 1 = rotation, 2 = scale
    int sampler = -1;
};

struct GltfAnimation {
    std::string name;
    float duration = 0.0f;
    std::vector<GltfAnimationSampler> samplers;
    std::vector<GltfAnimationChannel> channels;
};

/**
 * @brief Shared, immutable model data (geometry, materials, clips, skeleton).
 *
 * Load one per asset and draw it through any number of GltfInstance objects.
 * The destructor releases the GL resources, so destroy it on the GL thread.
 */
class GltfModel {
public:
    GltfModel();
    ~GltfModel();

    GltfModel(const GltfModel&) = delete;
    GltfModel& operator=(const GltfModel&) = delete;

    /// Load a `.glb` or `.gltf` file. Returns false and fills error() on failure.
    bool load(const std::string& path);

    bool isLoaded() const { return loaded_; }
    const std::string& error() const { return error_; }

    int animationCount() const { return static_cast<int>(animations_.size()); }
    const std::string& animationName(int index) const;
    float animationDuration(int index) const;
    /// @return clip index or -1. Comparison is case-insensitive.
    int findAnimation(const std::string& name) const;

    int nodeCount() const { return static_cast<int>(nodes_.size()); }
    const GltfNode& node(int index) const { return nodes_[static_cast<std::size_t>(index)]; }
    /// @return node index by name (case-insensitive) or -1.
    int findNode(const std::string& name) const;

    /// Internal: called by Renderer after the instance pose has been updated.
    void draw(const class GltfInstance& instance, Shader& shader,
              const glm::mat4& transform, const glm::vec3& tint,
              const glm::mat4& viewProjection) const;

private:
    friend class GltfInstance;

    bool loaded_ = false;
    std::string error_;

    std::vector<GltfMesh> meshes_;
    std::vector<GltfMaterial> materials_;
    std::vector<GltfNode> nodes_;
    std::vector<GltfSkin> skins_;
    std::vector<GltfAnimation> animations_;
    std::vector<int> roots_;       ///< scene root nodes (topological roots)
    std::vector<int> order_;       ///< parents before children
    std::vector<int> meshNodes_;   ///< node indices that carry a mesh
    std::vector<GLuint> textures_; ///< owned GL textures (for cleanup)
};

/**
 * @brief Per-object animation state for a GltfModel.
 *
 * Instances are cheap and default-constructible; call init() with the shared
 * model, then set the desired pose each frame and call update() before
 * submitting it to the renderer.
 */
class GltfInstance {
public:
    GltfInstance() = default;

    void init(const GltfModel* model);
    bool valid() const { return model_ != nullptr && model_->loaded_; }
    const GltfModel* model() const { return model_; }

    /**
     * @brief Blend up to two clips (rest pose for negative indices).
     *
     * Translation/scale lerp and rotation slerp, with the combined weight
     * blended against the rest pose. Times are in seconds and are clamped to
     * each clip's duration; wrap them yourself for looping clips.
     */
    void setPose(int clipA, float timeA, float weightA,
                 int clipB = -1, float timeB = 0.0f, float weightB = 0.0f);

    /// Evaluate the current pose and rebuild the node/skin matrices.
    void update();

    /// Add a local rotation / translation on top of the evaluated clip pose.
    /// Used for stance poses (bent knees, prone) the asset has no clips for.
    /// Overrides are reset by clearNodeOverrides() (call once per frame).
    void clearNodeOverrides();
    void setNodeRotationOffset(int nodeIndex, const glm::quat& delta);
    void setNodeTranslationOffset(int nodeIndex, const glm::vec3& delta);

    const glm::mat4& globalTransform(int nodeIndex) const;
    const std::vector<glm::mat4>& globalTransforms() const { return globals_; }

private:
    friend class GltfModel;

    struct LocalTRS {
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
    };

    struct NodeOverride {
        bool hasRotation = false;
        glm::quat rotationDelta{1.0f, 0.0f, 0.0f, 0.0f};
        bool hasTranslation = false;
        glm::vec3 translationDelta{0.0f};
    };

    const GltfModel* model_ = nullptr;
    std::vector<LocalTRS> rest_;       ///< rest pose per node
    std::vector<LocalTRS> pose_;       ///< evaluated per-node pose
    std::vector<NodeOverride> overrides_; ///< per-node stance offsets
    std::vector<glm::mat4> globals_;   ///< world matrix per node
    bool poseDirty_ = true;

    static void evaluateClip(const GltfAnimation& animation,
                             float time, std::vector<LocalTRS>& pose);
    static glm::mat4 localMatrix(const LocalTRS& local);
    void rebuildGlobals();

    // Requested cross-fade, consumed by update().
    int clipA_ = -1;
    int clipB_ = -1;
    float timeA_ = 0.0f;
    float timeB_ = 0.0f;
    float weightA_ = 0.0f;
    float weightB_ = 0.0f;
};

#endif  // CUBE_ENGINE_GLTF_MODEL_HPP
