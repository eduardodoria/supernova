#include "ParticleMod.h"

using namespace Supernova;

ParticleMod::ParticleMod(){
    this->fromLife = 0;
    this->toLife = 0;
    this->value = -1;
}

ParticleMod::ParticleMod(float fromLife, float toLife){
    this->fromLife = fromLife;
    this->toLife = toLife;
    this->value = -1;
}

ParticleMod::~ParticleMod(){

}

void ParticleMod::execute(Particles* particles, int particle, float life){
    if ((fromLife != toLife) && (life <= fromLife) && (life >= toLife)) {
        value = (life - fromLife) / (toLife - fromLife);
    }else{
        value = -1;
    }
}