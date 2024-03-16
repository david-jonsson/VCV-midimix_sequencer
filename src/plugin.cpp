#include "plugin.hpp"

Plugin* instance;

void init(Plugin* p) 
{
    instance = p;
    p->addModel(model);
}
