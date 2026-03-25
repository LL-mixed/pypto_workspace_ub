#ifndef HW_MISC_LINQU_UB_BACKEND_H
#define HW_MISC_LINQU_UB_BACKEND_H

#include "qemu/osdep.h"

typedef struct LinquUbBackendOps {
    void *opaque;

    /*
     * Bind a guest-visible endpoint id to a host-side entity/session.
     */
    int (*register_endpoint)(void *opaque,
                             uint16_t endpoint_id,
                             uint32_t entity_id);

    int (*get_default_segment)(void *opaque,
                               uint16_t endpoint_id,
                               uint64_t *segment_out);

    /*
     * Submit one raw descriptor slot to the host-side simulator backend.
     * The backend is expected to decode the slot using the same fixed-width
     * descriptor layout as `sim-qemu`.
     */
    int (*submit_slot)(void *opaque,
                       uint16_t endpoint_id,
                       const uint8_t *slot,
                       size_t slot_len);

    /*
     * Ring the backend after one or more descriptors have been submitted from
     * guest memory. Returns the number of descriptor submissions accepted and
     * the number of descriptors still pending on the host side.
     */
    int (*ring_doorbell)(void *opaque,
                         uint16_t endpoint_id,
                         uint32_t max_batch,
                         uint32_t *submitted,
                         uint32_t *pending);

    /*
     * Poll one completion slot. Returns:
     *   0  when a completion slot was written to `slot_out`
     *   1  when no completion is currently available
     *  <0  on backend error
     */
    int (*poll_completion)(void *opaque,
                           uint16_t endpoint_id,
                           uint8_t *slot_out,
                           size_t slot_len);
} LinquUbBackendOps;

#endif
