#include "Model.h"

//
// (c) 2019 Eduardo Doria.
//

#include <istream>
#include <sstream>
#include "Log.h"
#include "render/ObjectRender.h"
#include <algorithm>
#include <float.h>
#include "tiny_obj_loader.h"
#include "tiny_gltf.h"
#include "util/ReadSModel.h"
#include "buffer/ExternalBuffer.h"
#include "action/MoveAction.h"
#include "action/RotateAction.h"
#include "action/ScaleAction.h"
#include "action/keyframe/KeyframeTrack.h"
#include "action/keyframe/TranslateTracks.h"
#include "action/keyframe/RotateTracks.h"
#include "action/keyframe/ScaleTracks.h"
#include "action/keyframe/MorphTracks.h"

using namespace Supernova;

Model::Model(): Mesh() {
    primitiveType = S_PRIMITIVE_TRIANGLES;
    submeshes.push_back(new Submesh());

    skeleton = NULL;
    gltfModel = NULL;

    skinning = false;
    morphTargets = false;

    buffers["vertices"] = &buffer;
    buffers["indices"] = &indices;

    buffer.addAttribute(S_VERTEXATTRIBUTE_VERTICES, 3);
    buffer.addAttribute(S_VERTEXATTRIBUTE_TEXTURECOORDS, 2);
    buffer.addAttribute(S_VERTEXATTRIBUTE_NORMALS, 3);
}

Model::Model(const char * path): Model() {
    filename = path;

}

Model::~Model() {
    if (gltfModel)
        delete gltfModel;

    clearAnimations();
}

bool Model::fileExists(const std::string &abs_filename, void *) {
    File df;
    int res = df.open(abs_filename.c_str());

    if (!res) {
        return true;
    }

    return false;
}

bool Model::readWholeFile(std::vector<unsigned char> *out, std::string *err, const std::string &filepath, void *) {
    Data filedata;

    if (filedata.open(filepath.c_str()) != FileErrors::NO_ERROR){
        Log::Error("Model file not found: %s", filepath.c_str());
        return false;
    }

    std::istringstream f(filedata.readString());

    if (!f) {
        if (err) {
            (*err) += "File open error : " + filepath + "\n";
        }
        return false;
    }

    f.seekg(0, f.end);
    size_t sz = static_cast<size_t>(f.tellg());
    f.seekg(0, f.beg);

    if (int(sz) < 0) {
        if (err) {
            (*err) += "Invalid file size : " + filepath +
                      " (does the path point to a directory?)";
        }
        return false;
    } else if (sz == 0) {
        if (err) {
            (*err) += "File is empty : " + filepath + "\n";
        }
        return false;
    }

    out->resize(sz);
    f.read(reinterpret_cast<char *>(&out->at(0)),
           static_cast<std::streamsize>(sz));
    //f.close();

    return true;
}

std::string Model::readFileToString(const char* filename){
    Data filedata;

    if (filedata.open(filename) != FileErrors::NO_ERROR){
        Log::Error("Model file not found: %s", filename);
        return "";
    }

    return filedata.readString();
}

bool Model::loadGLTFBuffer(int bufferViewIndex){
    const tinygltf::BufferView &bufferView = gltfModel->bufferViews[bufferViewIndex];
    const std::string name = getBufferName(bufferViewIndex);

    if (buffers.count(name) == 0 && bufferView.target != 0) {
        ExternalBuffer *ebuffer = new ExternalBuffer();

        buffers[name] = ebuffer;

        if (bufferView.target == 34962) { //GL_ARRAY_BUFFER
            ebuffer->setBufferType(S_BUFFERTYPE_VERTEX);
        } else if (bufferView.target == 34963) { //GL_ELEMENT_ARRAY_BUFFER
            ebuffer->setBufferType(S_BUFFERTYPE_INDEX);
        }

        ebuffer->setData(&gltfModel->buffers[bufferView.buffer].data.at(0) + bufferView.byteOffset, bufferView.byteLength);

        return true;
    }

    return false;
}

std::string Model::getBufferName(int bufferViewIndex){
    const tinygltf::BufferView &bufferView = gltfModel->bufferViews[bufferViewIndex];

    if (!bufferView.name.empty())
        return bufferView.name;
    else
        return "buffer"+std::to_string(bufferViewIndex);

}

Bone* Model::generateSketetalStructure(int nodeIndex, int skinIndex){
    tinygltf::Node node = gltfModel->nodes[nodeIndex];
    tinygltf::Skin skin = gltfModel->skins[skinIndex];

    int index = -1;

    for (int j = 0; j < skin.joints.size(); j++) {
        if (nodeIndex == skin.joints[j])
            index = j;
    }

    Matrix4 offsetMatrix;

    if (skin.inverseBindMatrices >= 0) {

        tinygltf::Accessor accessor = gltfModel->accessors[skin.inverseBindMatrices];
        tinygltf::BufferView bufferView = gltfModel->bufferViews[accessor.bufferView];

        float *matrices = (float *) (&gltfModel->buffers[bufferView.buffer].data.at(0) +
                                     bufferView.byteOffset + accessor.byteOffset +
                                     (16 * sizeof(float) * index));

        if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != TINYGLTF_TYPE_MAT4) {
            Log::Error("Skeleton error: Unknown inverse bind matrix data type");

            return NULL;
        }

        offsetMatrix = Matrix4(
                matrices[0], matrices[4], matrices[8], matrices[12],
                matrices[1], matrices[5], matrices[9], matrices[13],
                matrices[2], matrices[6], matrices[10], matrices[14],
                matrices[3], matrices[7], matrices[11], matrices[15]);

    }

    Bone* bone = new Bone();

    bone->setIndex(index);
    bone->setName(node.name);
    bone->setBindPosition(Vector3(node.translation[0], node.translation[1], node.translation[2]));
    bone->setBindRotation(Quaternion(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]));
    bone->setBindScale(Vector3(node.scale[0], node.scale[1], node.scale[2]));
    bone->moveToBind();

    bone->setOffsetMatrix(offsetMatrix);

    bonesNameMapping[bone->getName()] = bone;
    bonesIdMapping[nodeIndex] = bone;

    for (size_t i = 0; i < node.children.size(); i++){
        bone->addObject(generateSketetalStructure(node.children[i], skinIndex));
    }

    bone->model = this;

    return bone;
}

bool Model::loadGLTF(const char* filename) {

    if (!gltfModel)
        gltfModel = new tinygltf::Model();

    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    int meshIndex = 0;

    buffers.clear();

    loader.SetFsCallbacks({&fileExists, &tinygltf::ExpandFilePath, &readWholeFile, nullptr, nullptr});
    //loader.SetFsCallbacks({nullptr, nullptr, nullptr, nullptr, nullptr});

    std::string ext = FileData::getFilePathExtension(filename);

    bool res = false;

    if (ext.compare("glb") == 0) {
        res = loader.LoadBinaryFromFile(gltfModel, &err, &warn, filename); // for binary glTF(.glb)
    }else{
        res = loader.LoadASCIIFromFile(gltfModel, &err, &warn, filename);
    }

    if (!warn.empty()) {
        Log::Warn("Loading GLTF model (%s): %s", filename, warn.c_str());
    }

    if (!err.empty()) {
        Log::Error("Can't load GLTF model (%s): %s", filename, err.c_str());
        return false;
    }

    if (!res) {
        Log::Verbose("Failed to load glTF: %s", filename);
        return false;
    }

    tinygltf::Mesh mesh = gltfModel->meshes[meshIndex];

    resizeSubmeshes(mesh.primitives.size());

    for (size_t i = 0; i < mesh.primitives.size(); i++) {

        tinygltf::Primitive primitive = mesh.primitives[i];
        tinygltf::Accessor indexAccessor = gltfModel->accessors[primitive.indices];
        tinygltf::Material &mat = gltfModel->materials[primitive.material];

        DataType indexType;

        if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE){
            indexType = DataType::UNSIGNED_BYTE;
        }else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT){
            indexType = DataType::UNSIGNED_SHORT;
        }else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT){
            indexType = DataType::UNSIGNED_INT;
        }else{
            Log::Error("Unknown index type %i", indexAccessor.componentType);
            continue;
        }

        Material *material = submeshes[i]->getMaterial();

        for (auto &mats : mat.values) {
            //Log::Debug("mat: %s - %i - %f ", mats.first.c_str(), mats.second.TextureIndex(), mats.second.Factor());
            if (mats.first.compare("baseColorTexture") == 0){
                if (mats.second.TextureIndex() >= 0){
                    tinygltf::Texture &tex = gltfModel->textures[mats.second.TextureIndex()];
                    tinygltf::Image &image = gltfModel->images[tex.source];

                    size_t imageSize = image.component * image.width * image.height; //in bytes

                    int textureType = S_COLOR_RGB_ALPHA;
                    if (image.component == 3){
                        textureType = S_COLOR_RGB;
                    }else if (image.component == 2){
                        textureType = S_COLOR_GRAY_ALPHA;
                    }else if (image.component == 1){
                        textureType = S_COLOR_GRAY;
                    }

                    TextureData *textureData = new TextureData(image.width, image.height, imageSize, textureType, image.component, &image.image.at(0));

                    Texture *texture = new Texture(textureData, std::string(filename) + "|" + image.name);
                    texture->setDataOwned(true);

                    material->setTexture(texture);
                    material->setTextureOwned(true);
                }
            }

            if (mats.first.compare("baseColorFactor") == 0){
                if (mats.second.ColorFactor().size()){
                    material->setColor(Vector4(
                            mats.second.ColorFactor()[0],
                            mats.second.ColorFactor()[1],
                            mats.second.ColorFactor()[2],
                            mats.second.ColorFactor()[3]));
                }
            }
        }

        loadGLTFBuffer(indexAccessor.bufferView);

        submeshes[i]->setIndices(
                getBufferName(indexAccessor.bufferView),
                indexAccessor.count,
                indexAccessor.byteOffset,
                indexType);

        for (auto &attrib : primitive.attributes) {
            tinygltf::Accessor accessor = gltfModel->accessors[attrib.second];
            int byteStride = accessor.ByteStride(gltfModel->bufferViews[accessor.bufferView]);
            std::string bufferName = getBufferName(accessor.bufferView);

            loadGLTFBuffer(accessor.bufferView);

            int elements = 1;
            if (accessor.type != TINYGLTF_TYPE_SCALAR) {
                elements = accessor.type;
            }

            DataType dataType;

            if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_BYTE){
                dataType = DataType::BYTE;
            }else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE){
                dataType = DataType::UNSIGNED_BYTE;
            }else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_SHORT){
                dataType = DataType::SHORT;
            }else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT){
                dataType = DataType::UNSIGNED_SHORT;
            }else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT){
                dataType = DataType::UNSIGNED_INT;
            }else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT){
                dataType = DataType::FLOAT;
            }else{
                Log::Error("Unknown data type %i of %s", accessor.componentType, attrib.first.c_str());
                continue;
            }

            int attType = -1;
            if (attrib.first.compare("POSITION") == 0){
                defaultBuffer = bufferName;
                attType = S_VERTEXATTRIBUTE_VERTICES;
            }
            if (attrib.first.compare("NORMAL") == 0){
                attType = S_VERTEXATTRIBUTE_NORMALS;
            }
            if (attrib.first.compare("TANGENT") == 0){
            }
            if (attrib.first.compare("TEXCOORD_0") == 0){
                attType = S_VERTEXATTRIBUTE_TEXTURECOORDS;
            }
            if (attrib.first.compare("JOINTS_0") == 0){
                attType = S_VERTEXATTRIBUTE_BONEIDS;
            }
            if (attrib.first.compare("WEIGHTS_0") == 0){
                attType = S_VERTEXATTRIBUTE_BONEWEIGHTS;
            }

            if (attType > -1) {
                buffers[bufferName]->setRenderAttributes(false);
                submeshes[i]->addAttribute(bufferName, attType, elements, dataType, byteStride, accessor.byteOffset);
            } else
                Log::Warn("Model attribute missing: %s", attrib.first.c_str());
        }

        morphTargets = false;
        bool morphNormals = false;

        int morphIndex = 0;
        for (auto &morphs : primitive.targets) {
            for (auto &attribMorph : morphs) {

                morphTargets = true;

                tinygltf::Accessor accessor = gltfModel->accessors[attribMorph.second];
                int byteStride = accessor.ByteStride(gltfModel->bufferViews[accessor.bufferView]);
                std::string bufferName = getBufferName(accessor.bufferView);

                loadGLTFBuffer(accessor.bufferView);

                int elements = 1;
                if (accessor.type != TINYGLTF_TYPE_SCALAR) {
                    elements = accessor.type;
                }

                DataType dataType;

                if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_BYTE){
                    dataType = DataType::BYTE;
                }else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE){
                    dataType = DataType::UNSIGNED_BYTE;
                }else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_SHORT){
                    dataType = DataType::SHORT;
                }else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT){
                    dataType = DataType::UNSIGNED_SHORT;
                }else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT){
                    dataType = DataType::UNSIGNED_INT;
                }else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT){
                    dataType = DataType::FLOAT;
                }else{
                    Log::Error("Unknown data type %i of morph target %s", accessor.componentType, attribMorph.first.c_str());
                    continue;
                }

                int attType = -1;
                if (attribMorph.first.compare("POSITION") == 0){
                    if (morphIndex == 0){
                        attType = S_VERTEXATTRIBUTE_MORPHTARGET0;
                    } else if (morphIndex == 1){
                        attType = S_VERTEXATTRIBUTE_MORPHTARGET1;
                    } else if (morphIndex == 2){
                        attType = S_VERTEXATTRIBUTE_MORPHTARGET2;
                    } else if (morphIndex == 3){
                        attType = S_VERTEXATTRIBUTE_MORPHTARGET3;
                    }
                    if (!morphNormals){
                        if (morphIndex == 4){
                            attType = S_VERTEXATTRIBUTE_MORPHTARGET4;
                        } else if (morphIndex == 5){
                            attType = S_VERTEXATTRIBUTE_MORPHTARGET5;
                        } else if (morphIndex == 6){
                            attType = S_VERTEXATTRIBUTE_MORPHTARGET6;
                        } else if (morphIndex == 7){
                            attType = S_VERTEXATTRIBUTE_MORPHTARGET7;
                        }
                    }
                }
                if (attribMorph.first.compare("NORMAL") == 0){
                    morphNormals = true;
                    if (morphIndex == 0){
                        attType = S_VERTEXATTRIBUTE_MORPHNORMAL0;
                    } else if (morphIndex == 1){
                        attType = S_VERTEXATTRIBUTE_MORPHNORMAL1;
                    } else if (morphIndex == 2){
                        attType = S_VERTEXATTRIBUTE_MORPHNORMAL2;
                    } else if (morphIndex == 3){
                        attType = S_VERTEXATTRIBUTE_MORPHNORMAL3;
                    }
                }
                if (attribMorph.first.compare("TANGENT") == 0){
                }

                if (attType > -1) {
                    buffers[bufferName]->setRenderAttributes(false);
                    submeshes[i]->addAttribute(bufferName, attType, elements, dataType, byteStride, accessor.byteOffset);
                }
            }
            morphIndex++;
        }

        int morphWeightSize = 8;
        if (morphNormals){
            morphWeightSize = 4;
        }

        if (morphTargets){
            morphWeights.resize(morphWeightSize);
            for (int w = 0; w < mesh.weights.size(); w++) {
                if (w < morphWeightSize){
                    morphWeights[w] = mesh.weights[w];
                }
            }

            //Getting morph target names from mesh extra property
            morphNameMapping.clear();
            if (mesh.extras.Has("targetNames") && mesh.extras.Get("targetNames").IsArray()){
                for (int t = 0; t < mesh.extras.Get("targetNames").Size(); t++){
                    morphNameMapping[mesh.extras.Get("targetNames").Get(t).Get<std::string>()] = t;
                }
            }
        }
    }

    int skinIndex = -1;
    int skeletonRoot = -1;
    std::map<int, int> nodesParent;

    for (size_t i = 0; i < gltfModel->nodes.size(); i++) {
        nodesParent[i] = -1;
    }

    for (size_t i = 0; i < gltfModel->nodes.size(); i++) {
        tinygltf::Node node = gltfModel->nodes[i];

        if (node.mesh == meshIndex && node.skin >= 0){
            skinIndex = node.skin;
        }

        for (int c = 0; c < node.children.size(); c++){
            nodesParent[node.children[c]] = i;
        }
    }

    if (skinIndex >= 0){
        tinygltf::Skin skin = gltfModel->skins[skinIndex];

        if (skin.skeleton >= 0) {
            skeletonRoot = skin.skeleton;
        }else {
            //Find skeleton root
            for (int j = 0; j < skin.joints.size(); j++) {
                int nodeIndex = skin.joints[j];

                if (std::find(skin.joints.begin(), skin.joints.end(), nodesParent[nodeIndex]) == skin.joints.end())
                    skeletonRoot = nodeIndex;
            }
        }

        bonesNameMapping.clear();
        bonesIdMapping.clear();

        skeleton = generateSketetalStructure(skeletonRoot, skinIndex);

        if (skeleton) {
            bonesMatrix.resize(skin.joints.size());
            addObject(skeleton);
        }
    }

    clearAnimations();

    for (size_t i = 0; i < gltfModel->animations.size(); i++) {
        const tinygltf::Animation &animation = gltfModel->animations[i];

        Animation* anim = new Animation();
        anim->setName(animation.name);

        animations.push_back(anim);

        float startTime = FLT_MAX;
        float endTime = 0;

        for (size_t j = 0; j < animation.channels.size(); j++) {

            const tinygltf::AnimationChannel &channel = animation.channels[j];
            const tinygltf::AnimationSampler &sampler = animation.samplers[channel.sampler];

            tinygltf::Accessor accessorIn = gltfModel->accessors[sampler.input];
            tinygltf::BufferView bufferViewIn = gltfModel->bufferViews[accessorIn.bufferView];

            tinygltf::Accessor accessorOut = gltfModel->accessors[sampler.output];
            tinygltf::BufferView bufferViewOut = gltfModel->bufferViews[accessorOut.bufferView];

            //TODO: Implement rotation and weights non float
            if (accessorOut.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {

                if (accessorIn.count != accessorOut.count) {
                    Log::Error("Incorrect frame size in animation: %s, sampler: %i",
                               animation.name.c_str(), channel.sampler);
                }

                float *timeValues = (float *) (&gltfModel->buffers[bufferViewIn.buffer].data.at(0) +
                                               bufferViewIn.byteOffset + accessorIn.byteOffset);
                float *values = (float *) (&gltfModel->buffers[bufferViewOut.buffer].data.at(0) +
                                           bufferViewOut.byteOffset + accessorOut.byteOffset);

                float trackStartTime = timeValues[0];
                float trackEndTIme = timeValues[accessorIn.count - 1];

                if (trackStartTime < startTime)
                    startTime = trackStartTime;

                if (trackEndTIme > endTime)
                    endTime = trackEndTIme;

                KeyframeTrack *track = NULL;

                if (channel.target_path.compare("translation") == 0) {
                    track = new TranslateTracks();
                    for (int c = 0; c < accessorIn.count; c++) {
                        Vector3 positionAc(values[3 * c], values[(3 * c) + 1], values[(3 * c) + 2]);
                        ((TranslateTracks *) track)->addKeyframe(timeValues[c], positionAc);
                    }
                }
                if (channel.target_path.compare("rotation") == 0) {
                    track = new RotateTracks();
                    for (int c = 0; c < accessorIn.count; c++) {
                        Quaternion rotationAc(values[(4 * c) + 3], values[4 * c],
                                              values[(4 * c) + 1], values[(4 * c) + 2]);
                        ((RotateTracks *) track)->addKeyframe(timeValues[c], rotationAc);
                    }
                }
                if (channel.target_path.compare("scale") == 0) {
                    track = new ScaleTracks();
                    for (int c = 0; c < accessorIn.count; c++) {
                        Vector3 scaleAc(values[3 * c], values[(3 * c) + 1], values[(3 * c) + 2]);
                        ((ScaleTracks *) track)->addKeyframe(timeValues[c], scaleAc);
                    }
                }
                if (channel.target_path.compare("weights") == 0) {
                    track = new MorphTracks();
                    int morphNum = accessorOut.count / accessorIn.count;
                    for (int c = 0; c < accessorIn.count; c++) {
                        std::vector<float> weightsAc;
                        for (int m = 0; m < morphNum; m++) {
                            weightsAc.push_back(values[morphNum * c] + m);
                        }
                        ((MorphTracks *) track)->addKeyframe(timeValues[c], weightsAc);
                    }
                }

                if (track) {
                    track->setDuration(trackEndTIme - trackStartTime);
                    if (bonesIdMapping.count(channel.target_node)) {
                        anim->addActionFrame(trackStartTime, track, bonesIdMapping[channel.target_node]);
                    } else {
                        anim->addActionFrame(trackStartTime, track, this);
                    }
                }

            }else{
                Log::Error("Cannot load animation: %s, channel %i: no float elements", animation.name.c_str(), j);
            }
        }

        if (anim->getStartTime() < startTime)
            anim->setStartTime(startTime);

        if (anim->getEndTime() > endTime)
            anim->setEndTime(endTime);

        addAction(anim);

    }

/*
    //BEGIN DEBUG
    for (auto &mesh : gltfModel->meshes) {
        Log::Verbose("mesh : %s", mesh.name.c_str());
        for (auto &primitive : mesh.primitives) {
            const tinygltf::Accessor &indexAccessor = gltfModel->accessors[primitive.indices];

            Log::Verbose("indexaccessor: count %i, type %i", indexAccessor.count,
                    indexAccessor.componentType);

            tinygltf::Material &mat = gltfModel->materials[primitive.material];
            for (auto &mats : mat.values) {
                Log::Verbose("mat : %s", mats.first.c_str());
            }

            for (auto &image : gltfModel->images) {
                Log::Verbose("image name : %s", image.uri.c_str());
                Log::Verbose("  size : %i", image.image.size());
                Log::Verbose("  w/h : %i/%i", image.width, image.height);
            }

            Log::Verbose("indices : %i", primitive.indices);
            Log::Verbose("mode     : %i", primitive.mode);

            for (auto &attrib : primitive.attributes) {
                Log::Verbose("attribute : %s", attrib.first.c_str());
            }
        }
    }
    //END DEBUG
*/
    std::reverse(std::begin(submeshes), std::end(submeshes));

    return true;
}

bool Model::loadOBJ(const char* filename){

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warn;
    std::string err;
    
    tinyobj::FileReader::externalFunc = readFileToString;

    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename, baseDir.c_str());

    if (!warn.empty()) {
        Log::Warn("Loading OBJ model (%s): %s", filename, warn.c_str());
    }

    if (!err.empty()) {
        Log::Error("Can't load OBJ model (%s): %s", filename, err.c_str());
        return false;
    }

    if (ret) {

        buffer.clear();
        indices.clear();

        resizeSubmeshes(materials.size());

        for (size_t i = 0; i < materials.size(); i++) {
            this->submeshes[i]->getMaterial()->setTexturePath(baseDir+materials[i].diffuse_texname);
            if (materials[i].dissolve < 1){
                // TODO: Add this check on isTransparent Material method
                transparent = true;
            }
        }

        Attribute* attVertex = buffer.getAttribute(S_VERTEXATTRIBUTE_VERTICES);
        Attribute* attTexcoord = buffer.getAttribute(S_VERTEXATTRIBUTE_TEXTURECOORDS);
        Attribute* attNormal = buffer.getAttribute(S_VERTEXATTRIBUTE_NORMALS);

        std::vector<std::vector<unsigned int>> indexMap;
        if (materials.size() > 0) {
            indexMap.resize(materials.size());
        }else{
            indexMap.resize(1);
        }

        for (size_t i = 0; i < shapes.size(); i++) {

            size_t index_offset = 0;
            for (size_t f = 0; f < shapes[i].mesh.num_face_vertices.size(); f++) {
                size_t fnum = shapes[i].mesh.num_face_vertices[f];

                int material_id = shapes[i].mesh.material_ids[f];
                if (material_id < 0)
                    material_id = 0;

                // For each vertex in the face
                for (size_t v = 0; v < fnum; v++) {
                    tinyobj::index_t idx = shapes[i].mesh.indices[index_offset + v];

                    indexMap[material_id].push_back(buffer.getCount());

                    buffer.addVector3(attVertex,
                                          Vector3(attrib.vertices[3*idx.vertex_index+0],
                                                  attrib.vertices[3*idx.vertex_index+1],
                                                  attrib.vertices[3*idx.vertex_index+2]));

                    if (attrib.texcoords.size() > 0) {
                        buffer.addVector2(attTexcoord,
                                              Vector2(attrib.texcoords[2 * idx.texcoord_index + 0],
                                                      1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]));
                    }
                    if (attrib.normals.size() > 0) {
                        buffer.addVector3(attNormal,
                                              Vector3(attrib.normals[3 * idx.normal_index + 0],
                                                      attrib.normals[3 * idx.normal_index + 1],
                                                      attrib.normals[3 * idx.normal_index + 2]));
                    }
                }

                index_offset += fnum;
            }
        }

        for (size_t i = 0; i < submeshes.size(); i++) {
            submeshes[i]->setIndices("indices", indexMap[i].size(), indices.getCount() * sizeof(unsigned int));

            indices.setValues(indices.getCount(), indices.getAttribute(S_INDEXATTRIBUTE), indexMap[i].size(), (char*)&indexMap[i].front(), sizeof(unsigned int));
        }

        std::reverse(std::begin(submeshes), std::end(submeshes));
    }

    return true;
}

Bone* Model::findBone(Bone* bone, int boneIndex){
    if (bone->getIndex() == boneIndex){
        return bone;
    }else{
        for (size_t i = 0; i < bone->getObjects().size(); i++){
            Bone* childreturn = findBone((Bone*)bone->getObject(i), boneIndex);
            if (childreturn)
                return childreturn;
        }
    }

    return NULL;
}

Bone* Model::getBone(std::string name){
    if (bonesNameMapping.count(name))
        return bonesNameMapping[name];

    return NULL;
}

Animation* Model::getAnimation(int index){
    if (index >= 0 && index < animations.size()){
        return animations[index];
    }

    Log::Error("Animation out of ranger");
    return NULL;
}

Animation* Model::findAnimation(std::string name){
    for (int i = 0; i < animations.size(); i++){
        if (animations[i]->getName() == name){
            return animations[i];
        }
    }

    return NULL;
}

void Model::clearAnimations(){
    for (int i = 0; i < animations.size(); i++){
        delete animations[i];
    }
    animations.clear();
}

float Model::getMorphWeight(int index){
    if (index >= 0 && index < morphWeights.size()){
        return morphWeights[index];
    }

    Log::Error("Morph target %i out of index", index);
    return 0;
}

float Model::getMorphWeight(std::string name){
    if (morphNameMapping.count(name)){
        return getMorphWeight(morphNameMapping[name]);
    }

    Log::Error("Morph target %s not exist", name.c_str());
    return 0;
}

void Model::setMorphWeight(int index, float value){
    if (index >= 0 && index < morphWeights.size()){
        morphWeights[index] = value;
    }else{
        Log::Error("Morph target %i out of index", index);
    }
}

void Model::setMorphWeight(std::string name, float value){
    if (morphNameMapping.count(name)) {
        setMorphWeight(morphNameMapping[name], value);
    }else{
        Log::Error("Morph target %s not exist", name.c_str());
    }
}

void Model::updateBone(int boneIndex, Matrix4 skinning){
    if (boneIndex >= 0 && boneIndex < bonesMatrix.size())
        bonesMatrix[boneIndex] = skinning;
}

void Model::updateModelMatrix(){
    Mesh::updateModelMatrix();

    inverseDerivedTransform = (modelMatrix * Matrix4::translateMatrix(center)).inverse();
}

bool Model::renderLoad(bool shadow) {

    if (!shadow){

        instanciateRender();

        if (skinning){
            render->addProperty(S_PROPERTY_BONESMATRIX, S_PROPERTYDATA_MATRIX4, bonesMatrix.size(), &bonesMatrix.front());
        }

        if (morphTargets){
            render->addProperty(S_PROPERTY_MORPHWEIGHTS, S_PROPERTYDATA_FLOAT1, morphWeights.size(), &morphWeights.front());
        }

    } else {

        instanciateShadowRender();

        if (skinning){
            shadowRender->addProperty(S_PROPERTY_BONESMATRIX, S_PROPERTYDATA_MATRIX4, bonesMatrix.size(), &bonesMatrix.front());
        }

        if (morphTargets){
            shadowRender->addProperty(S_PROPERTY_MORPHWEIGHTS, S_PROPERTYDATA_FLOAT1, morphWeights.size(), &morphWeights.front());
        }

    }

    return Mesh::renderLoad(shadow);
}

bool Model::load(){

    baseDir = FileData::getBaseDir(filename);

    std::string ext = FileData::getFilePathExtension(filename);

    if (ext.compare("obj") == 0) {
        if (!loadOBJ(filename))
            return false;
    }else{
        if (!loadGLTF(filename))
            return false;
    }

    if (skeleton)
        skinning = true;

    return Mesh::load();
}

Matrix4 Model::getInverseDerivedTransform(){
    return inverseDerivedTransform;
}
