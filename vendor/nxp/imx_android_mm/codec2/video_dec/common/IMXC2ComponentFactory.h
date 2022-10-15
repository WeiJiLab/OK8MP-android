/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

#ifndef IMX_CODEC2_COMPONENT_FACTORY_H_
#define IMX_CODEC2_COMPONENT_FACTORY_H_

#include <C2ComponentFactory.h>

class IMXC2ComponentFactory : public C2ComponentFactory {
public:
    typedef ::C2ComponentFactory* (*IMXCreateCodec2FactoryFunc)(C2String name);
    typedef void (*IMXDestroyCodec2FactoryFunc)(::C2ComponentFactory*);
};


#endif // IMX_CODEC2_COMPONENT_FACTORY_H_
