#pragma once

#include "CKAll.h"

namespace vdev {

// Ballanced declares the complete CKGrid interface directly.
inline CKERROR GridAddClassification(CKGrid *grid, int classification) {
    return grid->AddClassification(classification);
}
inline CKERROR GridAddClassificationByName(CKGrid *grid, const char *name) {
    return grid->AddClassificationByName(const_cast<CKSTRING>(name));
}
inline CKERROR GridRemoveClassification(CKGrid *grid, int classification) {
    return grid->RemoveClassification(classification);
}
inline CKLayer *GridAddLayer(CKGrid *grid, int type, int format = CKGRID_LAYER_FORMAT_NORMAL) {
    return grid->AddLayer(type, format);
}
inline CKLayer *GridGetLayer(CKGrid *grid, int type) {
    return grid->GetLayer(type);
}
inline CKERROR GridRemoveLayer(CKGrid *grid, int type) {
    return grid->RemoveLayer(type);
}


} // namespace vdev
