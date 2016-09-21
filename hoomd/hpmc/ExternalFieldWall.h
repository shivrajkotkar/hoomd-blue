// Copyright (c) 2009-2016 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

#ifndef _EXTERNAL_FIELD_WALL_H_
#define _EXTERNAL_FIELD_WALL_H_

/*! \file ExternalField.h
    \brief Declaration of ExternalField base class
*/
#include "hoomd/Compute.h"
#include "hoomd/extern/saruprng.h" // not sure if we need this for the accept method
#include "hoomd/VectorMath.h"

#include "IntegratorHPMCMono.h"
#include "ExternalField.h"

#include <tuple>

#ifndef NVCC
#include <hoomd/extern/pybind/include/pybind11/pybind11.h>
#endif

namespace hpmc
{
template < unsigned int old_max_verts, unsigned int new_max_verts >
detail::poly3d_verts<new_max_verts> cast_poly3d_verts(const detail::poly3d_verts<old_max_verts>& old_verts)
    {
    // restricting this cast from small arrays to larger ones ok because it can not invalidate
    // any of the data. The otherway is not true and I don't want to worry about that
    // right now.
    #if  old_max_verts > new_max_verts
        #error "must cast to a larger number of vertices"
    #endif

    // All data guaranteed to be valid because of static_assert above
    detail::poly3d_verts<new_max_verts> verts;
    verts.N = old_verts.N;
    verts.diameter = old_verts.diameter;
    verts.sweep_radius = old_verts.sweep_radius;
    verts.ignore = old_verts.ignore;

    // initialize because we have observed strange behaviour if we don't.
    for (unsigned int i = 0; i < new_max_verts; i++)
        {
        if( i < old_verts.N )
            {
                verts.x[i] = old_verts.x[i];
                verts.y[i] = old_verts.y[i];
                verts.z[i] = old_verts.z[i];
            }
        else
            {
            verts.x[i] = verts.y[i] = verts.z[i] = OverlapReal(0);
            }
        }

    return verts;
    }

struct SphereWall
    {
    SphereWall() : rsq(0), inside(false), origin(0,0,0), verts(new detail::poly3d_verts<1>) {}
    SphereWall(Scalar r, vec3<Scalar> orig, bool ins = true) : rsq(r*r), inside(ins), origin(orig), verts(new detail::poly3d_verts<1>)
    {
        verts->N = 0; // case for sphere (can be 0 or 1)
        verts->diameter = r+r;
        verts->sweep_radius = r;
        verts->ignore = 0;
        // verts->x[0] = verts->y[0] = verts->z[0] = OverlapReal(0);
    }
    SphereWall(const SphereWall& src) : rsq(src.rsq), inside(src.inside), origin(src.origin), verts(new detail::poly3d_verts<1>(*src.verts)) {}
    // scale all distances associated with the sphere wall by some factor alpha
    void scale(const OverlapReal& alpha)
        {
        rsq *= alpha*alpha;
        origin *= alpha;
        verts->diameter *= alpha;
        verts->sweep_radius *= alpha;
        }

    OverlapReal          rsq;
    bool            inside;
    vec3<OverlapReal>    origin;
    std::shared_ptr<detail::poly3d_verts<1> >    verts;
    };

struct CylinderWall
    {
    CylinderWall() : rsq(0), inside(false), origin(0,0,0), orientation(origin), verts(new detail::poly3d_verts<2>) {}
    CylinderWall(Scalar r, vec3<Scalar> orig, vec3<Scalar> orient, bool ins = true) : rsq(0), inside(false), origin(0,0,0), orientation(origin), verts(new detail::poly3d_verts<2>)
        {

        rsq = r*r;
        inside = ins;
        origin = orig;
        orientation = orient;

        OverlapReal len = sqrt(dot(orientation, orientation)); // normalize the orientation vector
        orientation /= len;

        // set the position of the vertices and the diameter later
        verts->N = 2;
        verts->sweep_radius = r;
        verts->ignore = 0;

        // set all vertex values to 0 to ensure no memory initialization issues
        // only need to do this for the first 2 vertices, due to the way
        // ShapeConvexPolyhedron.h finds support vectors

        // for (unsigned int i = 0; i < 2; i++)
        // {
        //     verts.x[i] = verts.y[i] = verts.z[i] = OverlapReal(0);
        // }

        }
    CylinderWall(const CylinderWall& src) : rsq(src.rsq), inside(src.inside), origin(src.origin), orientation(src.orientation), verts(new detail::poly3d_verts<2>(*src.verts)) {}
    // scale all distances associated with the sphere wall by some factor alpha
    void scale(const OverlapReal& alpha)
        {
        rsq *= alpha*alpha;
        origin *= alpha;
        verts->sweep_radius *= alpha;
        }

    OverlapReal          rsq;
    bool            inside;
    vec3<OverlapReal>    origin;         // center of cylinder.
    vec3<OverlapReal>    orientation;    // (normal) vector pointing in direction of long axis of cylinder (sign of vector has no meaning)
    std::shared_ptr<detail::poly3d_verts<2> >    verts;
    };

struct PlaneWall
    {
    PlaneWall(vec3<Scalar> nvec, vec3<Scalar> pt, bool ins = true) : normal(nvec), origin(pt), inside(ins)
    {
        OverlapReal len = sqrt(dot(normal, normal));
        normal /= len;
        d = -dot(normal, origin);
    }

    // scale all distances associated with the sphere wall by some factor alpha
    void scale(const OverlapReal& alpha)
        {
        origin *= alpha;
        d *= alpha;
        }

    vec3<OverlapReal>    normal; // unit normal n = (a, b, c)
    vec3<OverlapReal>    origin; // we could remove this.
    bool            inside; // not used
    OverlapReal          d;      // ax + by + cz + d =  0
    };

template <class WallShape, class ParticleShape>
DEVICE inline bool test_confined(const WallShape& wall, const ParticleShape& shape, const vec3<Scalar>& position, const vec3<Scalar>& box_origin, const BoxDim& box)
    {
    return false;
    }

// Spherical Walls and Spheres
template < >
DEVICE inline bool test_confined<SphereWall, ShapeSphere>(const SphereWall& wall, const ShapeSphere& shape, const vec3<Scalar>& position, const vec3<Scalar>& box_origin, const BoxDim& box)
    {
    Scalar3 t = vec_to_scalar3(position - box_origin);
    t.x  = t.x - wall.origin.x;
    t.y  = t.y - wall.origin.y;
    t.z  = t.z - wall.origin.z;
    vec3<OverlapReal> shifted_pos(box.minImage(t));

    OverlapReal rxyz_sq = shifted_pos.x*shifted_pos.x + shifted_pos.y*shifted_pos.y + shifted_pos.z*shifted_pos.z; // distance from the container origin.
    OverlapReal max_dist = sqrt(rxyz_sq) + (shape.getCircumsphereDiameter()/OverlapReal(2.0));
    if (!wall.inside)
      {
      // if we must be outside the wall, subtract particle radius from min_dist
      max_dist = sqrt(rxyz_sq) - (shape.getCircumsphereDiameter()/OverlapReal(2.0));
      // if the particle radius is larger than the distance between the particle
      // and the container, however, then ALWAYS check verts. this is equivalent
      // to two particle circumspheres overlapping.
      if (max_dist < 0)
        {
        max_dist = OverlapReal(0);
        }
      }

    return wall.inside ? (wall.rsq > max_dist*max_dist) : (wall.rsq < max_dist*max_dist);
    }

// Spherical Walls and Convex Polyhedra
template < unsigned int max_verts >
DEVICE inline bool test_confined(const SphereWall& wall, const ShapeConvexPolyhedron<max_verts>& shape, const vec3<Scalar>& position, const vec3<Scalar>& box_origin, const BoxDim& box) // <SphereWall, ShapeConvexPolyhedron<max_verts> >
    {
    bool accept = true;
    Scalar3 t = vec_to_scalar3(position - box_origin);
    t.x  = t.x - wall.origin.x;
    t.y  = t.y - wall.origin.y;
    t.z  = t.z - wall.origin.z;
    vec3<OverlapReal> shifted_pos(box.minImage(t));

    OverlapReal rxyz_sq = shifted_pos.x*shifted_pos.x + shifted_pos.y*shifted_pos.y + shifted_pos.z*shifted_pos.z;
    OverlapReal max_dist = (sqrt(rxyz_sq) + shape.getCircumsphereDiameter()/OverlapReal(2.0));
    if (!wall.inside)
      {
      // if we must be outside the wall, subtract particle radius from min_dist
      max_dist = sqrt(rxyz_sq) - (shape.getCircumsphereDiameter()/OverlapReal(2.0));
      // if the particle radius is larger than the distance between the particle
      // and the container, however, then ALWAYS check verts. this is equivalent
      // to two particle circumspheres overlapping.
      if (max_dist < 0)
        {
        max_dist = OverlapReal(0);
        }
      }

    bool check_verts = wall.inside ? (wall.rsq <= max_dist*max_dist) : (wall.rsq >= max_dist*max_dist); // condition to check vertices, dependent on inside or outside container


    if (check_verts)
        {
        if(wall.inside)
            {
            for(size_t v = 0; v < shape.verts.N && accept; v++)
                {
                vec3<OverlapReal> pos(shape.verts.x[v], shape.verts.y[v], shape.verts.z[v]);
                vec3<OverlapReal> rotated_pos = rotate(quat<OverlapReal>(shape.orientation), pos);
                rotated_pos += shifted_pos;
                rxyz_sq = rotated_pos.x*rotated_pos.x + rotated_pos.y*rotated_pos.y + rotated_pos.z*rotated_pos.z;
                accept = wall.inside ? (wall.rsq > rxyz_sq) : (wall.rsq < rxyz_sq);
                }
            }
        else
            {
            // build a sphero-polyhedron and for the wall and the convex polyhedron

            quat<OverlapReal> q; // default is (1, 0, 0, 0)
            unsigned int err = 0;
            ShapeSpheropolyhedron<max_verts> wall_shape(q, cast_poly3d_verts<1, max_verts>(*wall.verts));
            ShapeSpheropolyhedron<max_verts> part_shape(shape.orientation, shape.verts);

/*
            vec3<OverlapReal> dr = shifted_pos;
            OverlapReal DaDb = wall_shape.getCircumsphereDiameter() + part_shape.getCircumsphereDiameter();
            accept = !xenocollide_3d(detail::SupportFuncSpheropolyhedron(wall_shape.verts),
                                  detail::SupportFuncSpheropolyhedron(part_shape.verts),
                                  rotate(conj(quat<OverlapReal>(wall_shape.orientation)),dr),
                                  conj(quat<OverlapReal>(wall_shape.orientation)) * quat<OverlapReal>(part_shape.orientation),
                                  DaDb/2.0,
                                  err);
*/
            accept = !test_overlap(shifted_pos, wall_shape, part_shape, err);
            }
        }
    return accept;
    }

// Cylindrical Walls and Spheres
template < >
DEVICE inline bool test_confined<CylinderWall, ShapeSphere>(const CylinderWall& wall, const ShapeSphere& shape, const vec3<Scalar>& position, const vec3<Scalar>& box_origin, const BoxDim& box)
    {
    Scalar3 t = vec_to_scalar3(position - box_origin);
    t.x = t.x - wall.origin.x;
    t.y = t.y - wall.origin.y;
    t.z = t.z - wall.origin.z;
    vec3<OverlapReal> shifted_pos(box.minImage(t));

    vec3<OverlapReal> dist_vec = cross(shifted_pos, wall.orientation); // find the component of the shifted position that is perpendicular to the normalized orientation vector
    OverlapReal max_dist = sqrt(dot(dist_vec, dist_vec));
    if (wall.inside) {max_dist += (shape.getCircumsphereDiameter()/OverlapReal(2.0));} // add the circumradius of the particle if the particle must be inside the wall
    else
      {
      // subtract the circumradius of the particle if it must be outside the wall
      max_dist -= (shape.getCircumsphereDiameter()/OverlapReal(2.0));
      // if the particle radius is larger than the distance between the particle
      // and the container, however, then ALWAYS check verts. this is equivalent
      // to two particle circumspheres overlapping.
      if (max_dist < 0)
        {
        max_dist = OverlapReal(0);
        }
      }

    return wall.inside ? (wall.rsq > max_dist*max_dist) : (wall.rsq < max_dist*max_dist);
    }

// Cylindrical Walls and Convex Polyhedra
template < unsigned int max_verts >
DEVICE inline bool test_confined(const CylinderWall& wall, const ShapeConvexPolyhedron<max_verts>& shape, const vec3<Scalar>& position, const vec3<Scalar>& box_origin, const BoxDim& box) // <CylinderWall, ShapeConvexPolyhedron<max_verts> >
    {
    bool accept = true;
    Scalar3 t = vec_to_scalar3(position - box_origin);
    t.x = t.x - wall.origin.x;
    t.y = t.y - wall.origin.y;
    t.z = t.z - wall.origin.z;
    vec3<OverlapReal> shifted_pos(box.minImage(t));

    vec3<OverlapReal> dist_vec = cross(shifted_pos, wall.orientation); // find the component of the shifted position that is perpendicular to the normalized orientation vector
    OverlapReal max_dist = sqrt(dot(dist_vec, dist_vec));
    if (wall.inside) {max_dist += (shape.getCircumsphereDiameter()/OverlapReal(2.0));} // add the circumradius of the particle if the particle must be inside the wall
    else
      {
      // subtract the circumradius of the particle if it must be outside the wall
      max_dist -= (shape.getCircumsphereDiameter()/OverlapReal(2.0));
      // if the particle radius is larger than the distance between the particle
      // and the container, however, then ALWAYS check verts. this is equivalent
      // to two particle circumspheres overlapping.
      if (max_dist < 0)
        {
        max_dist = OverlapReal(0);
        }
      }

    bool check_verts = wall.inside ? (wall.rsq <= max_dist*max_dist) : (wall.rsq >= max_dist*max_dist); // condition to check vertic
    if (check_verts)
        {
        if(wall.inside)
            {
            for (size_t v = 0; v < shape.verts.N && accept; v++)
                {
                vec3<OverlapReal> pos(shape.verts.x[v], shape.verts.y[v], shape.verts.z[v]);
                vec3<OverlapReal> rotated_pos = rotate(quat<OverlapReal>(shape.orientation), pos);
                rotated_pos += shifted_pos;

                dist_vec = cross(rotated_pos, wall.orientation);
                max_dist = sqrt(dot(dist_vec, dist_vec));
                accept = wall.inside ? (wall.rsq > max_dist*max_dist) : (wall.rsq < max_dist*max_dist);
                }
            }
        else
            {
            // build a spheropolyhedron for the wall and the convex polyhedron
            // set the vertices and diamter for the wall.

            vec3<OverlapReal> r_ab, proj;
            proj = dot(shifted_pos, wall.orientation)*wall.orientation;
            r_ab = shifted_pos - proj;
            unsigned int err = 0;
            assert(shape.verts.sweep_radius == 0);
            ShapeSpheropolyhedron<max_verts> wall_shape(quat<OverlapReal>(), cast_poly3d_verts<2, max_verts>(*wall.verts));
            ShapeSpheropolyhedron<max_verts> part_shape(quat<OverlapReal>(shape.orientation), shape.verts);
            accept = !test_overlap(r_ab, wall_shape, part_shape, err);
            }
        }
    return accept;
    }

// Plane Walls and Spheres
template < >
DEVICE inline bool test_confined<PlaneWall, ShapeSphere>(const PlaneWall& wall, const ShapeSphere& shape, const vec3<Scalar>& position, const vec3<Scalar>& box_origin, const BoxDim& box)
    {
    Scalar3 t = vec_to_scalar3(position - box_origin);
    vec3<OverlapReal> shifted_pos(box.minImage(t));
    OverlapReal max_dist = dot(wall.normal, shifted_pos) + wall.d; // proj onto unit normal. (signed distance)
    return (max_dist < 0) ? false :  0 < (max_dist - shape.getCircumsphereDiameter()/OverlapReal(2.0));
    }

// Plane Walls and Convex Polyhedra
template < unsigned int max_verts >
DEVICE inline bool test_confined(const PlaneWall& wall, const ShapeConvexPolyhedron<max_verts>& shape, const vec3<Scalar>& position, const vec3<Scalar>& box_origin, const BoxDim& box) // <PlaneWall, ShapeConvexPolyhedron<max_verts> >
    {
    bool accept = true;
    Scalar3 t = vec_to_scalar3(position - box_origin);
    vec3<OverlapReal> shifted_pos(box.minImage(t));
    OverlapReal max_dist = dot(wall.normal, shifted_pos) + wall.d; // proj onto unit normal. (signed distance)
    accept = OverlapReal(0.0) < max_dist; // center is on the correct side of the plane.
    if( accept && (max_dist <= shape.getCircumsphereDiameter()/OverlapReal(2.0))) // should this be <= for consistency? should never matter... it was previously just <
        {
        for(size_t v = 0; v < shape.verts.N && accept; v++)
            {
            vec3<OverlapReal> pos(shape.verts.x[v], shape.verts.y[v], shape.verts.z[v]);
            vec3<OverlapReal> rotated_pos = rotate(quat<OverlapReal>(shape.orientation), pos);
            rotated_pos += shifted_pos;
            max_dist = dot(wall.normal, rotated_pos) + wall.d;  // proj onto unit normal. (signed distance)
            accept = OverlapReal(0.0) < max_dist;            // vert is on the correct side of the plane.
            }
        }
    return accept;
    }

template< class Shape >
class ExternalFieldWall : public ExternalFieldMono<Shape>
    {
        using Compute::m_pdata;
    public:
        ExternalFieldWall(std::shared_ptr<SystemDefinition> sysdef, std::shared_ptr<IntegratorHPMCMono<Shape> > mc) : ExternalFieldMono<Shape>(sysdef), m_mc(mc)
          {
          m_box = m_pdata->getGlobalBox();
          //! scale the container walls every time the box changes
          m_pdata->getBoxChangeSignal().template connect<ExternalFieldWall<Shape>, &ExternalFieldWall<Shape>::scaleWalls>(this);
          }
        ~ExternalFieldWall()
          {
          m_pdata->getBoxChangeSignal().template disconnect<ExternalFieldWall<Shape>, &ExternalFieldWall<Shape>::scaleWalls>(this);
          }

        bool accept(const unsigned int& index, const vec3<Scalar>& position_old, const Shape& shape_old, const vec3<Scalar>& position_new, const Shape& shape_new, Saru&)
            {
            return fabs(boltzmann(index, position_old, shape_old, position_new, shape_new) - Scalar(1.0)) < SMALL;
            }

        Scalar boltzmann(const unsigned int& index, const vec3<Scalar>& position_old, const Shape& shape_old, const vec3<Scalar>& position_new, const Shape& shape_new)
            {
            const BoxDim& box = this->m_pdata->getGlobalBox();
            vec3<Scalar> origin(m_pdata->getOrigin());

            for(size_t i = 0; i < m_Spheres.size(); i++)
                {
                if(!test_confined(m_Spheres[i], shape_new, position_new, origin, box))
                    {
                    return Scalar(0.0);
                    }
                }

            for(size_t i = 0; i < m_Cylinders.size(); i++)
                {
                set_cylinder_wall_verts(m_Cylinders[i], shape_new);
                if(!test_confined(m_Cylinders[i], shape_new, position_new, origin, box))
                    {
                    return Scalar(0.0);
                    }
                }

            for(size_t i = 0; i < m_Planes.size(); i++)
                {
                if(!test_confined(m_Planes[i], shape_new, position_new, origin, box))
                    {
                    return Scalar(0.0);
                    }
                }

            return Scalar(1.0);
            }

        Scalar calculateBoltzmannWeight(unsigned int timestep)
            {
            unsigned int numOverlaps = countOverlaps(timestep, false);
            if(numOverlaps > 0)
                {
                return Scalar(0.0);
                }
            else
                {
                return Scalar(1.0);
                }
            }

        Scalar calculateBoltzmannFactor(const Scalar4* const position_old,
                                        const Scalar4* const orientation_old,
                                        const BoxDim* const box_old
                                        )
            {
            unsigned int numOverlaps = countOverlaps(0, false);
            if(numOverlaps > 0)
                {
                return Scalar(0.0);
                }
            else
                {
                return Scalar(1.0);
                }
            }

        // assumes CUBIC box
        void scaleWalls()
            {
            BoxDim newBox = m_pdata->getGlobalBox();
            Scalar newVol = newBox.getVolume();
            Scalar oldVol = m_box.getVolume();
            OverlapReal alpha = pow((newVol/oldVol), Scalar(1.0/3.0));
            m_Volume *= (newVol/oldVol);

            for(size_t i = 0; i < m_Spheres.size(); i++)
                {
                m_Spheres[i].scale(alpha);
                }

            for(size_t i = 0; i < m_Cylinders.size(); i++)
                {
                m_Cylinders[i].scale(alpha);
                }

            for(size_t i = 0; i < m_Planes.size(); i++)
                {
                m_Planes[i].scale(alpha);
                }


            m_box = newBox;
            }

        std::tuple<OverlapReal, vec3<OverlapReal>, bool> GetSphereWallParameters(size_t index)
            {
            SphereWall w = GetSphereWall(index);
            return std::make_tuple(w.rsq, w.origin, w.inside);
            }

        pybind11::tuple GetSphereWallParametersPy(size_t index)
            {
            OverlapReal rsq; vec3<OverlapReal> origin; bool inside;
            pybind11::list pyorigin;
            std::tuple<OverlapReal, vec3<OverlapReal>, bool> t = GetSphereWallParameters(index);
            std::tie(rsq, origin, inside) = t;
            pyorigin.append(pybind11::cast(origin.x)); pyorigin.append(pybind11::cast(origin.y)); pyorigin.append(pybind11::cast(origin.z));
            return pybind11::make_tuple(rsq, pyorigin, inside);
            }

        std::tuple<OverlapReal, vec3<OverlapReal>, vec3<OverlapReal>, bool> GetCylinderWallParameters(size_t index)
            {
            CylinderWall w = GetCylinderWall(index);
            return std::make_tuple(w.rsq, w.origin, w.orientation, w.inside);
            }

        pybind11::tuple GetCylinderWallParametersPy(size_t index)
            {
            OverlapReal rsq; vec3<OverlapReal> origin; vec3<OverlapReal> orientation; bool inside;
            pybind11::list pyorigin; pybind11::list pyorientation;
            std::tuple<OverlapReal, vec3<OverlapReal>, vec3<OverlapReal>, bool> t = GetCylinderWallParameters(index);
            std::tie(rsq, origin, orientation, inside) = t;
            pyorigin.append(pybind11::cast(origin.x)); pyorigin.append(pybind11::cast(origin.y)); pyorigin.append(pybind11::cast(origin.z));
            pyorientation.append(pybind11::cast(orientation.x)); pyorientation.append(pybind11::cast(orientation.y)); pyorientation.append(pybind11::cast(orientation.z));
            return pybind11::make_tuple(rsq, pyorigin, pyorientation, inside);
            }

        std::tuple<vec3<OverlapReal>, vec3<OverlapReal> > GetPlaneWallParameters(size_t index)
            {
            PlaneWall w = GetPlaneWall(index);
            return std::make_tuple(w.normal, w.origin);
            }

        pybind11::tuple GetPlaneWallParametersPy(size_t index)
            {
            vec3<OverlapReal> normal; vec3<OverlapReal> origin;
            pybind11::list pynormal; pybind11::list pyorigin;
            std::tuple<vec3<OverlapReal>, vec3<OverlapReal> > t = GetPlaneWallParameters(index);
            std::tie(normal, origin) = t;
            pynormal.append(pybind11::cast(normal.x)); pynormal.append(pybind11::cast(normal.y)); pynormal.append(pybind11::cast(normal.z));
            pyorigin.append(pybind11::cast(origin.x)); pyorigin.append(pybind11::cast(origin.y)); pyorigin.append(pybind11::cast(origin.z));
            return pybind11::make_tuple(pynormal, pyorigin);
            }

        const std::vector<SphereWall>& GetSphereWalls()
            {
            return m_Spheres;
            }

        const SphereWall& GetSphereWall(size_t index)
            {
            if(index >= m_Spheres.size())
                throw std::runtime_error("Out of bounds of sphere walls.");
            return m_Spheres[index];
            }

        const std::vector<CylinderWall>& GetCylinderWalls()
            {
            return m_Cylinders;
            }

        const CylinderWall& GetCylinderWall(size_t index)
            {
            if(index >= m_Cylinders.size())
                throw std::runtime_error("Out of bounds of cylinder walls.");
            return m_Cylinders[index];
            }

        const std::vector<PlaneWall>& GetPlaneWalls()
            {
            return m_Planes;
            }

        const PlaneWall& GetPlaneWall(size_t index)
            {
            if(index >= m_Planes.size())
                throw std::runtime_error("Out of bounds of plane walls.");
            return m_Planes[index];
            }

        void SetSphereWallParameter(size_t index, const SphereWall& wall)
            {
            if(index >= m_Spheres.size())
                throw std::runtime_error("Out of bounds of sphere walls.");
            m_Spheres[index] = wall;
            }

        void SetCylinderWallParameter(size_t index, const CylinderWall& wall)
            {
            if(index >= m_Cylinders.size())
                throw std::runtime_error("Out of bounds of cylinder walls.");
            m_Cylinders[index] = wall;
            }

        void SetPlaneWallParameter(size_t index, const PlaneWall& wall)
            {
            if(index >= m_Planes.size())
                throw std::runtime_error("Out of bounds of plane walls.");
            m_Planes[index] = wall;
            }

        void SetSphereWalls(const std::vector<SphereWall>& Spheres)
            {
            m_Spheres = Spheres;
            }

        void SetCylinderWalls(const std::vector<CylinderWall>& Cylinders)
            {
            m_Cylinders = Cylinders;
            }

        void SetPlaneWalls(const std::vector<PlaneWall>& Planes)
            {
            m_Planes = Planes;
            }

        void AddSphereWall(const SphereWall& wall)
            {
            m_Spheres.push_back(wall);
            unsigned int wall_ind = m_Spheres.size()-1;
            m_SphereLogQuantities.push_back(getSphWallParamName(wall_ind));
            }

        void AddCylinderWall(const CylinderWall& wall)
            {
            m_Cylinders.push_back(wall);
            unsigned int wall_ind = m_Cylinders.size()-1;
            m_CylinderLogQuantities.push_back(getCylWallParamName(wall_ind));
            }

        void AddPlaneWall(const PlaneWall& wall)
            {
            m_Planes.push_back(wall);
            }

        // is this messy ...
        void RemoveSphereWall(size_t index)
            {
            m_Spheres.erase(m_Spheres.begin()+index);
            m_SphereLogQuantities.erase(m_SphereLogQuantities.begin()+index);
            }

        void RemoveCylinderWall(size_t index)
            {
            m_Cylinders.erase(m_Cylinders.begin()+index);
            m_CylinderLogQuantities.erase(m_CylinderLogQuantities.begin()+index);
            }

        void RemovePlaneWall(size_t index)
            {
            m_Planes.erase(m_Planes.begin()+index);
            }

        virtual std::vector< std::string > getProvidedLogQuantities()
            {
            std::vector<std::string> m_WallLogQuantities;
            m_WallLogQuantities.reserve(m_SphereLogQuantities.size() + m_CylinderLogQuantities.size());
            m_WallLogQuantities.insert(m_WallLogQuantities.end(), m_SphereLogQuantities.begin(), m_SphereLogQuantities.end());
            m_WallLogQuantities.insert(m_WallLogQuantities.end(), m_CylinderLogQuantities.begin(), m_CylinderLogQuantities.end());
            m_WallLogQuantities.push_back("hpmc_wall_volume");
            return m_WallLogQuantities;
            }

        virtual Scalar getLogValue(const std::string& quantity, unsigned int timestep)
            {
            for (size_t i = 0; i < m_Spheres.size(); i++)
              {
              if (quantity == getSphWallParamName(i))
                {
                return m_Spheres[i].rsq;
                }
              }
            for (size_t i = 0; i < m_Cylinders.size(); i++)
              {
              if (quantity == getCylWallParamName(i))
                {
                return m_Cylinders[i].rsq;
                }
              }
            if (quantity == "hpmc_wall_volume")
            {
              return m_Volume;
            }
            this->m_exec_conf->msg->error() << "compute.wall: " << quantity << " is not a valid log quantity." << std::endl;
            throw std::runtime_error("Error getting log value");
            }

        unsigned int countOverlaps(unsigned int timestep, bool early_exit = false)
            {
            unsigned int numOverlaps = 0;
            // access particle data and system box
            ArrayHandle<Scalar4> h_postype(m_pdata->getPositions(), access_location::host, access_mode::readwrite);
            ArrayHandle<Scalar4> h_orientation(m_pdata->getOrientationArray(), access_location::host, access_mode::readwrite);
            ArrayHandle<typename Shape::param_type> h_params(m_mc->getParams(), access_location::host, access_mode::read);

            for(unsigned int i = 0; i < m_pdata->getN(); i++)
                {
                // read in the current position and orientation
                Scalar4 postype_i = h_postype.data[i];
                Scalar4 orientation_i = h_orientation.data[i];
                vec3<Scalar> pos_i = vec3<Scalar>(postype_i);
                int typ_i = __scalar_as_int(postype_i.w);
                Shape shape_i(quat<Scalar>(orientation_i), h_params.data[typ_i]);
                numOverlaps += (unsigned int) (1 - int(boltzmann(i, pos_i, shape_i, pos_i, shape_i)));
                if(early_exit && numOverlaps > 0)
                    {
                    numOverlaps = 1;
                    break;
                    }
                }

            #ifdef ENABLE_MPI
            if (this->m_pdata->getDomainDecomposition())
                {
                MPI_Allreduce(MPI_IN_PLACE, &numOverlaps, 1, MPI_UNSIGNED, MPI_SUM, this->m_exec_conf->getMPICommunicator());
                if (early_exit && numOverlaps > 1)
                    numOverlaps = 1;
                }
            #endif

            return numOverlaps;

            }

        void setVolume(Scalar volume) { m_Volume = volume; }

        Scalar getVolume() { return m_Volume; }

        unsigned int getNumSphereWalls() {return m_Spheres.size(); }
        unsigned int getNumCylinderWalls() {return m_Cylinders.size(); }
        unsigned int getNumPlaneWalls() {return m_Planes.size(); }

        bool hasVolume() { return true; }

        Scalar GetCurrBoxLx() { return m_box.getL().x; }
        Scalar GetCurrBoxLy() { return m_box.getL().y; }
        Scalar GetCurrBoxLz() { return m_box.getL().z; }
        Scalar GetCurrBoxTiltFactorXY() { return m_box.getTiltFactorXY(); }
        Scalar GetCurrBoxTiltFactorXZ() { return m_box.getTiltFactorXZ(); }
        Scalar GetCurrBoxTiltFactorYZ() { return m_box.getTiltFactorYZ(); }

        void SetCurrBox(Scalar Lx, Scalar Ly, Scalar Lz, Scalar xy, Scalar xz, Scalar yz)
            {
            m_box.setL(make_scalar3(Lx, Ly, Lz));
            m_box.setTiltFactors(xy,xz,yz);
            }

    protected:
        void set_cylinder_wall_verts(CylinderWall& wall, const Shape& shape)
            {
            vec3<Scalar> v0;
            v0 = shape.getCircumsphereDiameter()*wall.orientation;
            wall.verts->x[0] = -v0.x;
            wall.verts->y[0] = -v0.y;
            wall.verts->z[0] = -v0.z;

            wall.verts->x[1] = v0.x;
            wall.verts->y[1] = v0.y;
            wall.verts->z[1] = v0.z;
            wall.verts->diameter = 2.0*(shape.getCircumsphereDiameter()+wall.verts->sweep_radius);
            }
        std::string getSphWallParamName(size_t i)
          {
          std::stringstream ss;
          std::string snum;
          ss << i;
          ss >> snum;
          return "hpmc_wall_sph_rsq-" + snum;
          }
        std::string getCylWallParamName(size_t i)
          {
          std::stringstream ss;
          std::string snum;
          ss << i;
          ss >> snum;
          return "hpmc_wall_cyl_rsq-" + snum;
          }
    protected:
        std::vector<SphereWall>     m_Spheres;
        std::vector<CylinderWall>   m_Cylinders;
        std::vector<PlaneWall>      m_Planes;
        std::vector<std::string>    m_SphereLogQuantities;
        std::vector<std::string>    m_CylinderLogQuantities;
        Scalar                      m_Volume;
    private:
        std::shared_ptr<IntegratorHPMCMono<Shape> > m_mc; //!< integrator
        BoxDim                                        m_box; //!< the current box
    };



template<class Shape>
void export_ExternalFieldWall(pybind11::module& m, const std::string& name)
{
   pybind11::class_< ExternalFieldWall<Shape>, std::shared_ptr< ExternalFieldWall<Shape> > >(m, name.c_str(), pybind11::base< ExternalFieldMono<Shape> >())
    .def(pybind11::init< std::shared_ptr<SystemDefinition>, std::shared_ptr< IntegratorHPMCMono<Shape> > >())
    .def("SetSphereWallParameter", &ExternalFieldWall<Shape>::SetSphereWallParameter)
    .def("SetCylinderWallParameter", &ExternalFieldWall<Shape>::SetCylinderWallParameter)
    .def("SetPlaneWallParameter", &ExternalFieldWall<Shape>::SetPlaneWallParameter)
    .def("AddSphereWall", &ExternalFieldWall<Shape>::AddSphereWall)
    .def("AddCylinderWall", &ExternalFieldWall<Shape>::AddCylinderWall)
    .def("AddPlaneWall", &ExternalFieldWall<Shape>::AddPlaneWall)
    .def("RemoveSphereWall", &ExternalFieldWall<Shape>::RemoveSphereWall)
    .def("RemoveCylinderWall", &ExternalFieldWall<Shape>::RemoveCylinderWall)
    .def("RemovePlaneWall", &ExternalFieldWall<Shape>::RemovePlaneWall)
    .def("countOverlaps", &ExternalFieldWall<Shape>::countOverlaps)
    .def("setVolume", &ExternalFieldWall<Shape>::setVolume)
    .def("getVolume", &ExternalFieldWall<Shape>::getVolume)
    .def("getNumSphereWalls", &ExternalFieldWall<Shape>::getNumSphereWalls)
    .def("getNumCylinderWalls", &ExternalFieldWall<Shape>::getNumCylinderWalls)
    .def("getNumPlaneWalls", &ExternalFieldWall<Shape>::getNumPlaneWalls)
    .def("GetSphereWallParametersPy", &ExternalFieldWall<Shape>::GetSphereWallParametersPy)
    .def("GetCylinderWallParametersPy", &ExternalFieldWall<Shape>::GetCylinderWallParametersPy)
    .def("GetPlaneWallParametersPy", &ExternalFieldWall<Shape>::GetPlaneWallParametersPy)
    .def("GetCurrBoxLx", &ExternalFieldWall<Shape>::GetCurrBoxLx)
    .def("GetCurrBoxLy", &ExternalFieldWall<Shape>::GetCurrBoxLy)
    .def("GetCurrBoxLz", &ExternalFieldWall<Shape>::GetCurrBoxLz)
    .def("GetCurrBoxTiltFactorXY", &ExternalFieldWall<Shape>::GetCurrBoxTiltFactorXY)
    .def("GetCurrBoxTiltFactorXZ", &ExternalFieldWall<Shape>::GetCurrBoxTiltFactorXZ)
    .def("GetCurrBoxTiltFactorYZ", &ExternalFieldWall<Shape>::GetCurrBoxTiltFactorYZ)
    .def("SetCurrBox", &ExternalFieldWall<Shape>::SetCurrBox)
    ;
}

} // namespace hpmc

#endif // inclusion guard
