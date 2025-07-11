//---------------------------------------------------------------------------
#pragma once

//#include		"skeletoncustom.h"
#include "bone.h"
#include "skeletonmotiondefs.h"
// refs
class IKinematicsAnimated;
class CBlend;
class IKinematics;

// callback
typedef void (*PlayCallback)(CBlend* P);

//*** Key frame definition ************************************************************************
enum
{
    flTKeyPresent = (1 << 0),
    flRKeyAbsent = (1 << 1),
    flTKey16IsBit = (1 << 2),
};
#pragma pack(push, 2)
struct CKey
{
    Fquaternion Q; // rotation
    Fvector T; // translation
};
struct CKeyQR
{
    s16 x, y, z, w; // rotation
};
struct CKeyQT8
{
    s8 x1, y1, z1;
};
struct CKeyQT16
{
    s16 x1, y1, z1;
};
/*
struct  CKeyQT
{
//	s8			x,y,z;
    s16			x1,y1,z1;
};
*/
#pragma pack(pop)

//*** Motion Data *********************************************************************************
class ENGINE_API CMotion
{
    struct
    {
        u32 _flags : 8;
        u32 _count : 24;
    };

public:
    ref_smem<CKeyQR> _keysR;
    ref_smem<CKeyQT8> _keysT8;
    ref_smem<CKeyQT16> _keysT16;
    Fvector _initT;
    Fvector _sizeT;

public:
    void set_flags(u8 val) { _flags = val; }
    void set_flag(u8 mask, u8 val)
    {
        if (val)
            _flags |= mask;
        else
            _flags &= ~mask;
    }
    BOOL test_flag(u8 mask) const { return BOOL(_flags & mask); }

    void set_count(u32 cnt)
    {
        VERIFY(cnt);
        _count = cnt;
    }
    ICF u32 get_count() const { return (u32(_count) & 0x00FFFFFF); }

    float GetLength() { return float(_count) * SAMPLE_SPF; }

    u32 mem_usage()
    {
        u32 sz = sizeof(*this);
        if (_keysR.size())
            sz += _keysR.size() * sizeof(CKeyQR) / _keysR.ref_count();
        if (_keysT8.size())
            sz += _keysT8.size() * sizeof(CKeyQT8) / _keysT8.ref_count();
        if (_keysT16.size())
            sz += _keysT16.size() * sizeof(CKeyQT16) / _keysT16.ref_count();
        return sz;
    }
};

class ENGINE_API motion_marks
{
public:
    typedef std::pair<float, float> interval;
private:
    typedef xr_vector<interval> STORAGE;
    typedef STORAGE::iterator ITERATOR;
    typedef STORAGE::const_iterator C_ITERATOR;

    STORAGE intervals;

public:
    shared_str name;
    void Load(IReader*);

    bool is_empty() const { return intervals.empty(); }
    const interval* pick_mark(float const& t) const;
    bool is_mark_between(float const& t0, float const& t1) const;
    float time_to_next_mark(float time) const;
};

class ENGINE_API CMotionDef
{
    float speed{}, power{}, accrue{}, falloff{};

public:
    u16 bone_or_part;
    u16 motion;
    u16 flags;
    xr_vector<motion_marks> marks;

    void Load(IReader* MP, u32 fl, u16 vers);
    u32 mem_usage() { return sizeof(*this); }

    inline float Accrue() const { return accrue; }
    inline float Falloff() const { return falloff; }
    inline float Speed() const { return speed; }
    inline float Power() const { return power; }
    bool StopAtEnd() const;
};

using accel_map = string_unordered_map<shared_str, u16>;

DEFINE_VECTOR(CMotionDef, MotionDefVec, MotionDefVecIt);

DEFINE_VECTOR(CMotion, MotionVec, MotionVecIt);
DEFINE_VECTOR(MotionVec*, BoneMotionsVec, BoneMotionsVecIt);

// partition
class ENGINE_API CPartDef
{
public:
    shared_str Name;
    xr_vector<u32> bones;
    CPartDef() : Name(nullptr){};

    u32 mem_usage() { return sizeof(*this) + bones.size() * sizeof(u32) + sizeof(Name); }
};
class ENGINE_API CPartition
{
    CPartDef P[MAX_PARTS];

public:
    IC CPartDef& operator[](u16 id) { return P[id]; }
    IC const CPartDef& part(u16 id) const { return P[id]; }
    u16 part_id(const shared_str& name) const;
    u32 mem_usage() { return P[0].mem_usage() * MAX_PARTS; }
    void load(IKinematics* V, LPCSTR model_name);
    u8 count() const
    {
        u8 ret = 0;
        for (u8 i = 0; i < MAX_PARTS; ++i)
            if (P[i].Name.size())
                ret++;
        return ret;
    };
};

// shared motions
struct ENGINE_API motions_value
{
    accel_map m_motion_map; // motion associations
    accel_map m_cycle; // motion data itself	(shared)
    accel_map m_fx; // motion data itself	(shared)
    CPartition m_partition; // partition
    u32 m_dwReference;
    string_unordered_map<shared_str, MotionVec> m_motions;
    MotionDefVec m_mdefs;

    shared_str m_id;

    BOOL load(LPCSTR N, IReader* data, vecBones* bones);
    MotionVec* bone_motions(const shared_str& bone_name);

    u32 mem_usage()
    {
        size_t sz = sizeof(*this) + m_motion_map.size() * 6 + m_partition.mem_usage();
        for (auto& m_mdef : m_mdefs)
            sz += m_mdef.mem_usage();
        for (auto& m_motion : m_motions)
            for (MotionVecIt m_it = m_motion.second.begin(); m_it != m_motion.second.end(); ++m_it)
                sz += m_it->mem_usage();
        return u32(sz);
    }
};

class ENGINE_API motions_container
{
    string_unordered_map<shared_str, motions_value*> container;

public:
    motions_container();
    ~motions_container();
    bool has(shared_str key);
    motions_value* dock(shared_str key, IReader* data, vecBones* bones);
    void dump();
    void clean(bool force_destroy);
};

extern ENGINE_API motions_container* g_pMotionsContainer;

class ENGINE_API shared_motions
{
private:
    motions_value* p_;

protected:
    // ref-counting
    void destroy()
    {
        if (nullptr == p_)
            return;
        p_->m_dwReference--;
        if (0 == p_->m_dwReference)
            p_ = nullptr;
    }

public:
    bool create(shared_str key, IReader* data,
                vecBones* bones); //{	motions_value* v = g_pMotionsContainer->dock(key,data,bones); if (0!=v) v->m_dwReference++; destroy(); p_ = v;	}
    bool create(shared_motions const& rhs); //	{	motions_value* v = rhs.p_; if (0!=v) v->m_dwReference++; destroy(); p_ = v;	}
public:
    // construction
    shared_motions() { p_ = nullptr; }
    shared_motions(shared_motions const& rhs)
    {
        p_ = nullptr;
        create(rhs);
    }
    ~shared_motions() { destroy(); }

    // assignment & accessors
    shared_motions& operator=(shared_motions const& rhs)
    {
        create(rhs);
        return *this;
    }
    bool operator==(shared_motions const& rhs) const { return (p_ == rhs.p_); }

    // misc func
    MotionVec* bone_motions(const shared_str& bone_name)
    {
        VERIFY(p_);
        return p_->bone_motions(bone_name);
    }
    accel_map* motion_map()
    {
        VERIFY(p_);
        return &p_->m_motion_map;
    }
    accel_map* cycle()
    {
        VERIFY(p_);
        return &p_->m_cycle;
    }
    accel_map* fx()
    {
        VERIFY(p_);
        return &p_->m_fx;
    }
    CPartition* partition()
    {
        VERIFY(p_);
        return &p_->m_partition;
    }
    CMotionDef* motion_def(u16 idx)
    {
        VERIFY(p_);
        return &p_->m_mdefs[idx];
    }

    const shared_str& id() const
    {
        VERIFY(p_);
        return p_->m_id;
    }
};
//---------------------------------------------------------------------------
