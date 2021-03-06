#include "GraphicObject.h"
#include "Scene.h"
#include "Log.h"

//
// (c) 2018 Eduardo Doria.
//

using namespace Supernova;

GraphicObject::GraphicObject(): Object(){
    visible = true;
    transparent = false;
    distanceToCamera = -1;

    minBufferSize = 0;

    render = NULL;
    shadowRender = NULL;

    material = NULL;

    scissor = Rect(0, 0, 0, 0);

    body = NULL;
}

GraphicObject::~GraphicObject(){
    if (material)
        delete material;
}

void GraphicObject::instanciateMaterial(){
    if (!material)
        material = new Material();
}

bool GraphicObject::instanciateRender(){
    if (render == NULL) {
        render = ObjectRender::newInstance();
        if (render == NULL)
            return false;
    }

    return true;
}

bool GraphicObject::instanciateShadowRender(){
    if (shadowRender == NULL) {
        shadowRender = ObjectRender::newInstance();
        if (shadowRender == NULL)
            return false;
    }

    return true;
}

void GraphicObject::setColor(Vector4 color){
    instanciateMaterial();

    if (color.w != 1){
        transparent = true;
    }
    material->setColor(color);
}

void GraphicObject::setColor(float red, float green, float blue, float alpha){
    setColor(Vector4(red, green, blue, alpha));
}

Vector4 GraphicObject::getColor(){
    instanciateMaterial();

    return *material->getColor();
}

void GraphicObject::setTexture(Texture* texture){
    instanciateMaterial();

    Texture* oldTexture = material->getTexture();

    if (texture != oldTexture){

        material->setTexture(texture);

        textureLoad();
    }
}

void GraphicObject::setTexture(std::string texturepath){
    instanciateMaterial();

    std::string oldTexture = material->getTexturePath();

    if (texturepath != oldTexture){

        material->setTexturePath(texturepath);

        textureLoad();
    }
}

std::string GraphicObject::getTexture(){
    instanciateMaterial();

    return material->getTexturePath();
}

Material* GraphicObject::getMaterial(){
    instanciateMaterial();

    return this->material;
}

void GraphicObject::updateBuffer(std::string name){
    if (name == defaultBuffer) {
        if (render)
            render->setVertexSize(buffers[name]->getCount());
        if (shadowRender)
            shadowRender->setVertexSize(buffers[name]->getCount());
    }
    if (render)
        render->updateBuffer(name, buffers[name]->getSize(), buffers[name]->getData());
    if (shadowRender)
        shadowRender->updateBuffer(name, buffers[name]->getSize(), buffers[name]->getData());
}

Matrix4 GraphicObject::getNormalMatrix(){
    return normalMatrix;
}

unsigned int GraphicObject::getMinBufferSize(){
    return minBufferSize;
}

void GraphicObject::setVisible(bool visible){
    this->visible = visible;
}

bool GraphicObject::isVisible(){
    return visible;
}

void GraphicObject::updateDistanceToCamera(){
    distanceToCamera = (this->cameraPosition - this->getWorldPosition()).length();
}

void GraphicObject::setSceneTransparency(bool transparency){
    if (scene) {
        if (scene->getUserDefinedTransparency() != S_OPTION_NO)
            scene->useTransparency = transparency;
    }
}

void GraphicObject::updateVPMatrix(Matrix4* viewMatrix, Matrix4* projectionMatrix, Matrix4* viewProjectionMatrix, Vector3* cameraPosition){
    Object::updateVPMatrix(viewMatrix, projectionMatrix, viewProjectionMatrix, cameraPosition);

    updateDistanceToCamera();
}

void GraphicObject::updateModelMatrix(){
    Object::updateModelMatrix();
    
    this->normalMatrix.identity();

    updateDistanceToCamera();
}

bool GraphicObject::draw(){

    bool drawReturn = false;

    if (scene && scene->isDrawingShadow()){
        renderDraw(true);
    }else{
        if (transparent && scene && scene->useDepth && distanceToCamera >= 0){
            scene->transparentQueue.insert(std::make_pair(distanceToCamera, this));
        }else{
            if (visible)
                renderDraw(false);
        }

        if (transparent){
            setSceneTransparency(true);
        }
    }

    if (!scissor.isZero() && scene){

        SceneRender* sceneRender = scene->getSceneRender();
        bool on = sceneRender->isEnabledScissor();
        Rect rect = sceneRender->getActiveScissor();

        if (on)
            scissor.fitOnRect(rect);

        sceneRender->enableScissor(scissor);

        drawReturn = Object::draw();

        if (!on)
            sceneRender->disableScissor();

    }else{

        drawReturn = Object::draw();
    }

    return drawReturn;
}

bool GraphicObject::load(){
    if (!Object::load()) {
        return false;
    }

    if (!renderLoad(false)) {
        loaded = false;
        return false;
    }

    //Check after texture is loaded
    if (material && material->isTransparent()){
        transparent = true;
    }

    setSceneTransparency(transparent);

    return true;
}

bool GraphicObject::textureLoad(){
    if (material && render){
        if (material->getTexture()->load())
            render->addTexture(S_TEXTURESAMPLER_DIFFUSE, material->getTexture());
    }

    return true;
}

bool GraphicObject::renderLoad(bool shadow){
    if (!shadow){
        instanciateRender();

        for (auto const& buf : buffers){
            if (buf.first == defaultBuffer) {
                render->setVertexSize(buf.second->getCount());
            }
            render->addBuffer(buf.first, buf.second->getSize(), buf.second->getData(), buf.second->getBufferType(), true);
            if (buf.second->isRenderAttributes()) {
                for (auto const &x : buf.second->getAttributes()) {
                    render->addVertexAttribute(x.first, buf.first, x.second.getElements(), x.second.getDataType(), x.second.getStride(), x.second.getOffset());
                }
            }
        }

        render->addProperty(S_PROPERTY_MODELMATRIX, S_PROPERTYDATA_MATRIX4, 1, &modelMatrix);
        render->addProperty(S_PROPERTY_NORMALMATRIX, S_PROPERTYDATA_MATRIX4, 1, &normalMatrix);
        render->addProperty(S_PROPERTY_MVPMATRIX, S_PROPERTYDATA_MATRIX4, 1, &modelViewProjectionMatrix);
        render->addProperty(S_PROPERTY_CAMERAPOS, S_PROPERTYDATA_FLOAT3, 1, &cameraPosition);

        if (material) {
            render->addTexture(S_TEXTURESAMPLER_DIFFUSE, material->getTexture());
            render->addProperty(S_PROPERTY_COLOR, S_PROPERTYDATA_FLOAT4, 1, material->getColor());
            if (material->getTextureRect())
                render->addProperty(S_PROPERTY_TEXTURERECT, S_PROPERTYDATA_FLOAT4, 1, material->getTextureRect());
        }

        if (scene){

            render->setSceneRender(scene->getSceneRender());

            scene->addLightProperties(render);
            scene->addFogProperties(render);
        }

        return render->load();

    } else{

        instanciateShadowRender();

        for (auto const& buf : buffers) {
            if (buf.first == defaultBuffer) {
                shadowRender->setVertexSize(buf.second->getCount());
            }
            shadowRender->addBuffer(buf.first, buf.second->getSize(), buf.second->getData(), buf.second->getBufferType(), true);
            if (buf.second->isRenderAttributes()) {
                for (auto const &x : buf.second->getAttributes()) {
                    shadowRender->addVertexAttribute(x.first, buf.first, x.second.getElements(), x.second.getDataType(), x.second.getStride(), x.second.getOffset());
                }
            }
        }

        shadowRender->addProperty(S_PROPERTY_MVPMATRIX, S_PROPERTYDATA_MATRIX4, 1, &modelViewProjectionMatrix);
        shadowRender->addProperty(S_PROPERTY_MODELMATRIX, S_PROPERTYDATA_MATRIX4, 1, &modelMatrix);
        shadowRender->addProperty(S_PROPERTY_CAMERAPOS, S_PROPERTYDATA_FLOAT3, 1, &cameraPosition);

        if (scene){
            shadowRender->addProperty(S_PROPERTY_SHADOWLIGHT_POS, S_PROPERTYDATA_FLOAT3, 1, &scene->drawShadowLightPos);
            shadowRender->addProperty(S_PROPERTY_SHADOWCAMERA_NEARFAR, S_PROPERTYDATA_FLOAT2, 1, &scene->drawShadowCameraNearFar);
            shadowRender->addProperty(S_PROPERTY_ISPOINTSHADOW, S_PROPERTYDATA_INT1, 1, &scene->drawIsPointShadow);
        }

        return shadowRender->load();

    }
}


bool GraphicObject::renderDraw(bool shadow){

    if (!visible)
        return false;

    if (!shadow) {

        render->prepareDraw();
        render->draw();
        render->finishDraw();

    }else{

        shadowRender->prepareDraw();
        shadowRender->draw();
        shadowRender->finishDraw();

    }

    return true;
}

void GraphicObject::destroy(){
    if (render)
        render->destroy();

    if (shadowRender)
        shadowRender->destroy();

    Object::destroy();
}
