#include "glo/session_warning.hpp"
#include <cassert>
int main() {
    glo::SessionWarningTracker t;
    assert(t.update(0,300,false)==0);
    assert(t.update(44,301,true)==0);
    assert(t.update(44,299,true)==5);
    assert(t.update(44,298,true)==0);
    assert(t.update(44,59,true)==1);
    assert(t.update(44,50,true)==0);
    assert(t.update(44,0,false)==0);
    assert(t.update(45,59,true)==1);
    assert(t.update(45,51,true)==0);
    assert(t.update(45,0,false)==0);
    assert(t.update(46,200,true)==5);
    assert(t.update(46,40,true)==1);
    return 0;
}
