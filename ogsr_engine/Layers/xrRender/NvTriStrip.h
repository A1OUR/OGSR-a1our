#pragma once

#ifndef NULL
#define NULL 0
#endif

////////////////////////////////////////////////////////////////////////////////////////
// Public interface for stripifier
////////////////////////////////////////////////////////////////////////////////////////

// GeForce1 and 2 cache size
#define CACHESIZE_GEFORCE1_2 16

// GeForce3 cache size
#define CACHESIZE_GEFORCE3 24

enum PrimType
{
    PT_LIST,
    PT_STRIP,
    PT_FAN
};

struct PrimitiveGroup
{
    PrimType type;
    unsigned int numIndices;
    unsigned short* indices;

    ////////////////////////////////////////////////////////////////////////////////////////

    PrimitiveGroup() : type(PT_STRIP), numIndices(0), indices(nullptr) {}
    ~PrimitiveGroup()
    {
        if (indices)
            xr_free(indices);
    }
};

////////////////////////////////////////////////////////////////////////////////////////
// SetCacheSize()
//
// Sets the cache size which the stripfier uses to optimize the data.
// Controls the length of the generated individual strips.
// This is the "actual" cache size, so 24 for GeForce3 and 16 for GeForce1/2
// You may want to play around with this number to tweak performance.
//
// Default value: 16
//
void SetCacheSize(const unsigned int cacheSize);

////////////////////////////////////////////////////////////////////////////////////////
// SetStitchStrips()
//
// bool to indicate whether to stitch together strips into one huge strip or not.
// If set to true, you'll get back one huge strip stitched together using degenerate
//  triangles.
// If set to false, you'll get back a large number of separate strips.
//
// Default value: true
//
void SetStitchStrips(const bool bStitchStrips);

////////////////////////////////////////////////////////////////////////////////////////
// SetMinStripSize()
//
// Sets the minimum acceptable size for a strip, in triangles.
// All strips generated which are shorter than this will be thrown into one big, separate list.
//
// Default value: 0
//
void SetMinStripSize(const unsigned int minSize);

////////////////////////////////////////////////////////////////////////////////////////
// SetListsOnly()
//
// If set to true, will return an optimized list, with no strips at all.
//
// Default value: false
//
void SetListsOnly(const bool bListsOnly);

////////////////////////////////////////////////////////////////////////////////////////
// GenerateStrips()
//
// in_indices: input index list, the indices you would use to render
// in_numIndices: number of entries in in_indices
// primGroups: array of optimized/stripified PrimitiveGroups
// numGroups: number of groups returned
//
// Be sure to call xr_free on the returned primGroups to avoid leaking mem
//
void GenerateStrips(const u16* in_indices, const s32 in_numIndices, xr_vector<PrimitiveGroup>& primGroups);

////////////////////////////////////////////////////////////////////////////////////////
// RemapIndices()
//
// Function to remap your indices to improve spatial locality in your vertex buffer.
//
// in_primGroups: array of PrimitiveGroups you want remapped
// numGroups: number of entries in in_primGroups
// numVerts: number of vertices in your vertex buffer, also can be thought of as the range
//  of acceptable values for indices in your primitive groups.
// remappedGroups: array of remapped PrimitiveGroups
//
// Note that, according to the remapping handed back to you, you must reorder your
//  vertex buffer.
//
// Credit goes to the MS Xbox crew for the idea for this interface.
//
void RemapIndices(const xr_vector<PrimitiveGroup>& in_primGroups, const u16 numVerts, xr_vector<PrimitiveGroup>& remappedGroups);
