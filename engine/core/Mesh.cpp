#include "Mesh.h"
#include "Scene.h"
#include "Log.h"

//
// (c) 2018 Eduardo Doria.
//

using namespace Supernova;

Mesh::Mesh(): ConcreteObject(){
    render = NULL;
    shadowRender = NULL;

    meshnodes.push_back(new MeshNode(&material));
    skymesh = false;
    textmesh = false;
    dynamic = false;
}

Mesh::~Mesh(){
    destroy();
    removeAllMeshNodes();

    if (render)
        delete render;

    if (shadowRender)
        delete shadowRender;
}

std::vector<Vector3> Mesh::getVertices(){
    return vertices;
}

std::vector<Vector3> Mesh::getNormals(){
    return normals;
}

std::vector<Vector2> Mesh::getTexcoords(){
    return texcoords;
}

std::vector<MeshNode*> Mesh::getMeshNodes(){
    return meshnodes;
}

bool Mesh::isSky(){
    return skymesh;
}

bool Mesh::isText(){
    return textmesh;
}

bool Mesh::isDynamic(){
    return dynamic;
}

int Mesh::getPrimitiveType(){
    return primitiveType;
}

void Mesh::setTexcoords(std::vector<Vector2> texcoords){
    this->texcoords.clear();
    this->texcoords = texcoords;
}

void Mesh::setPrimitiveType(int primitiveType){
    this->primitiveType = primitiveType;
}

void Mesh::addVertex(Vector3 vertex){
    vertices.push_back(vertex);
}

void Mesh::addNormal(Vector3 normal){
    normals.push_back(normal);
}

void Mesh::addTexcoord(Vector2 texcoord){
    texcoords.push_back(texcoord);
}

void Mesh::addMeshNode(MeshNode* meshnode){
    meshnodes.push_back(meshnode);
}

void Mesh::updateVertices(){
    render->setVertexSize(vertices.size());
    render->updateVertexBuffer("vertices", vertices.size() * 3 * sizeof(float), &vertices.front());
    if (shadowRender) {
        shadowRender->setVertexSize(vertices.size());
        shadowRender->updateVertexBuffer("vertices", vertices.size() * 3 * sizeof(float), &vertices.front());
    }
}

void Mesh::updateNormals(){
    render->updateVertexBuffer("normals", normals.size() * 3 * sizeof(float), &normals.front());
}

void Mesh::updateTexcoords(){
    render->updateVertexBuffer("texcoords", texcoords.size() * 2 * sizeof(float), &texcoords.front());
}

void Mesh::updateIndices(){
    for (size_t i = 0; i < meshnodes.size(); i++) {
        meshnodes[i]->getMeshNodeRender()->updateIndex(meshnodes[i]->getIndices()->size(), &(meshnodes[i]->getIndices()->front()));
        if (shadowRender)
            meshnodes[i]->getMeshNodeShadowRender()->updateIndex(meshnodes[i]->getIndices()->size(), &(meshnodes[i]->getIndices()->front()));
    }
}

void Mesh::sortTransparentMeshNodes(){
    if (transparent && scene && scene->isUseDepth() && scene->getUserDefinedTransparency() != S_OPTION_NO){

        bool needSort = false;
        for (size_t i = 0; i < meshnodes.size(); i++) {
            if (this->meshnodes[i]->getIndices()->size() > 0){
                Vector3 meshnodeFirstVertice = vertices[this->meshnodes[i]->getIndex(0)];
                meshnodeFirstVertice = modelMatrix * meshnodeFirstVertice;

                if (this->meshnodes[i]->getMaterial()->isTransparent()){
                    this->meshnodes[i]->distanceToCamera = (this->cameraPosition - meshnodeFirstVertice).length();
                    needSort = true;
                }
            }
        }
        
        if (needSort){
            std::sort(meshnodes.begin(), meshnodes.end(),
                      [](const MeshNode* a, const MeshNode* b) -> bool
                      {
                          if (a->distanceToCamera == -1)
                              return true;
                          if (b->distanceToCamera == -1)
                              return false;
                          return a->distanceToCamera > b->distanceToCamera;
                      });
            
        }
    }

}

void Mesh::updateVPMatrix(Matrix4* viewMatrix, Matrix4* projectionMatrix, Matrix4* viewProjectionMatrix, Vector3* cameraPosition){
    ConcreteObject::updateVPMatrix(viewMatrix, projectionMatrix, viewProjectionMatrix, cameraPosition);

    sortTransparentMeshNodes();
}

void Mesh::updateMatrix(){
    ConcreteObject::updateMatrix();
    
    this->normalMatrix = modelMatrix.inverse().transpose();

    sortTransparentMeshNodes();
}

void Mesh::removeAllMeshNodes(){
    for (std::vector<MeshNode*>::iterator it = meshnodes.begin() ; it != meshnodes.end(); ++it)
    {
        delete (*it);
    }
    meshnodes.clear();
}

bool Mesh::textureLoad(){
    if (!ConcreteObject::textureLoad())
        return false;
    
    for (size_t i = 0; i < meshnodes.size(); i++) {
        meshnodes[i]->textureLoad();
    }
    
    return true;
}

bool Mesh::shadowLoad(){
    if (!ConcreteObject::shadowLoad())
        return false;
    
    if (shadowRender == NULL)
        shadowRender = ObjectRender::newInstance();
    shadowRender->setProgramShader(S_SHADER_DEPTH_RTT);
    shadowRender->setVertexSize(vertices.size());
    shadowRender->addVertexBuffer("vertices", vertices.size() * 3 * sizeof(float), &vertices.front(), dynamic);
    shadowRender->addVertexAttribute(S_VERTEXATTRIBUTE_VERTICES, "vertices", 3);
    shadowRender->addProperty(S_PROPERTY_MVPMATRIX, S_PROPERTYDATA_MATRIX4, 1, &modelViewProjectionMatrix);
    shadowRender->addProperty(S_PROPERTY_MODELMATRIX, S_PROPERTYDATA_MATRIX4, 1, &modelMatrix);
    if (scene){
        shadowRender->addProperty(S_PROPERTY_SHADOWLIGHT_POS, S_PROPERTYDATA_FLOAT3, 1, &scene->drawShadowLightPos);
        shadowRender->addProperty(S_PROPERTY_SHADOWCAMERA_NEARFAR, S_PROPERTYDATA_FLOAT2, 1, &scene->drawShadowCameraNearFar);
        shadowRender->addProperty(S_PROPERTY_ISPOINTSHADOW, S_PROPERTYDATA_INT1, 1, &scene->drawIsPointShadow);
    }
    
    Program* shadowProgram = shadowRender->getProgram();
    
    for (size_t i = 0; i < meshnodes.size(); i++) {
        meshnodes[i]->dynamic = dynamic;
        if (meshnodes.size() == 1){
            //Use the same render for meshnode
            meshnodes[i]->setMeshNodeShadowRender(shadowRender);
        }else{
            meshnodes[i]->getMeshNodeShadowRender()->setProgram(shadowProgram);
        }
        meshnodes[i]->getMeshNodeShadowRender()->setPrimitiveType(primitiveType);
        meshnodes[i]->shadowLoad();
    }
    
    return shadowRender->load();
}

bool Mesh::load(){
    
    while (vertices.size() > texcoords.size()){
        texcoords.push_back(Vector2(0,0));
    }
    
    while (vertices.size() > normals.size()){
        normals.push_back(Vector3(0,0,0));
    }
    
    bool hasTextureRect = false;
    bool hasTextureCoords = false;
    bool hasTextureCube = false;
    for (unsigned int i = 0; i < meshnodes.size(); i++){
        if (meshnodes.at(i)->getMaterial()->getTextureRect()){
            hasTextureRect = true;
        }
        if (meshnodes.at(i)->getMaterial()->getTexture()){
            hasTextureCoords = true;
            if (meshnodes.at(i)->getMaterial()->getTexture()->getType() == S_TEXTURE_CUBE){
                hasTextureCube = true;
            }
        }
        if (meshnodes.at(i)->getMaterial()->isTransparent()) {
            transparent = true;
        }
    }
    
    if (render == NULL)
        render = ObjectRender::newInstance();

    render->setProgramShader(S_SHADER_MESH);
    render->setHasTextureCoords(hasTextureCoords);
    render->setHasTextureRect(hasTextureRect);
    render->setHasTextureCube(hasTextureCube);
    render->setIsSky(isSky());
    render->setIsText(isText());

    render->setVertexSize(vertices.size());

    render->addVertexBuffer("vertices", vertices.size() * 3 * sizeof(float), &vertices.front(), dynamic);
    render->addVertexAttribute(S_VERTEXATTRIBUTE_VERTICES,"vertices", 3);
    render->addVertexBuffer("normals", normals.size() * 3 * sizeof(float), &normals.front(), dynamic);
    render->addVertexAttribute(S_VERTEXATTRIBUTE_NORMALS, "normals", 3);
    render->addVertexBuffer("texcoords", texcoords.size() * 2 * sizeof(float), &texcoords.front(), dynamic);
    render->addVertexAttribute(S_VERTEXATTRIBUTE_TEXTURECOORDS, "texcoords", 2);
    
    render->addProperty(S_PROPERTY_MODELMATRIX, S_PROPERTYDATA_MATRIX4, 1, &modelMatrix);
    render->addProperty(S_PROPERTY_NORMALMATRIX, S_PROPERTYDATA_MATRIX4, 1, &normalMatrix);
    render->addProperty(S_PROPERTY_MVPMATRIX, S_PROPERTYDATA_MATRIX4, 1, &modelViewProjectionMatrix);
    render->addProperty(S_PROPERTY_CAMERAPOS, S_PROPERTYDATA_FLOAT3, 1, &cameraPosition); //TODO: put cameraPosition on Scene

    if (scene){

        render->setNumLights((int)scene->getLights()->size());
        render->setNumShadows2D(scene->getLightData()->numShadows2D);
        render->setNumShadowsCube(scene->getLightData()->numShadowsCube);

        render->setSceneRender(scene->getSceneRender());
        render->setLightRender(scene->getLightRender());
        render->setFogRender(scene->getFogRender());

        render->addTextureVector(S_TEXTURESAMPLER_SHADOWMAP2D, scene->getLightData()->shadowsMap2D);
        render->addProperty(S_PROPERTY_NUMSHADOWS2D, S_PROPERTYDATA_INT1, 1, &scene->getLightData()->numShadows2D);
        render->addProperty(S_PROPERTY_DEPTHVPMATRIX, S_PROPERTYDATA_MATRIX4, scene->getLightData()->numShadows2D, &scene->getLightData()->shadowsVPMatrix.front());
        render->addProperty(S_PROPERTY_SHADOWBIAS2D, S_PROPERTYDATA_FLOAT1, scene->getLightData()->numShadows2D, &scene->getLightData()->shadowsBias2D.front());
        render->addProperty(S_PROPERTY_SHADOWCAMERA_NEARFAR2D, S_PROPERTYDATA_FLOAT2, scene->getLightData()->numShadows2D, &scene->getLightData()->shadowsCameraNearFar2D.front());
        render->addProperty(S_PROPERTY_NUMCASCADES2D, S_PROPERTYDATA_INT1, scene->getLightData()->numShadows2D, &scene->getLightData()->shadowNumCascades2D.front());

        render->addTextureVector(S_TEXTURESAMPLER_SHADOWMAPCUBE, scene->getLightData()->shadowsMapCube);
        render->addProperty(S_PROPERTY_SHADOWBIASCUBE, S_PROPERTYDATA_FLOAT1, scene->getLightData()->numShadowsCube, &scene->getLightData()->shadowsBiasCube.front());
        render->addProperty(S_PROPERTY_SHADOWCAMERA_NEARFARCUBE, S_PROPERTYDATA_FLOAT2, scene->getLightData()->numShadowsCube, &scene->getLightData()->shadowsCameraNearFarCube.front());
    }
    
    for (size_t i = 0; i < meshnodes.size(); i++) {
        meshnodes[i]->dynamic = dynamic;
        if (meshnodes.size() == 1){
            //Use the same render for meshnode
            meshnodes[i]->setMeshNodeRender(render);
        }else{
            meshnodes[i]->getMeshNodeRender()->setParent(render);
        }
        meshnodes[i]->getMeshNodeRender()->setPrimitiveType(primitiveType);
        meshnodes[i]->load();
    }
    
    bool renderloaded = render->load();

    if (renderloaded)
        return ConcreteObject::load();
    else
        return false;
}

bool Mesh::shadowDraw(){
    if (!ConcreteObject::shadowDraw())
        return false;

    if (!visible)
        return false;

    shadowRender->prepareDraw();

    for (size_t i = 0; i < meshnodes.size(); i++) {
        meshnodes[i]->shadowDraw();
    }

    shadowRender->finishDraw();
 
    return true;
}

bool Mesh::renderDraw(){
    if (!ConcreteObject::renderDraw())
        return false;

    if (!visible)
        return false;

    render->prepareDraw();

    for (size_t i = 0; i < meshnodes.size(); i++) {
        meshnodes[i]->draw();
    }

    render->finishDraw();

    return true;
}

void Mesh::destroy(){
    ConcreteObject::destroy();
    
    for (size_t i = 0; i < meshnodes.size(); i++) {
        meshnodes[i]->destroy();
    }
    
    if (render)
        render->destroy();
}
