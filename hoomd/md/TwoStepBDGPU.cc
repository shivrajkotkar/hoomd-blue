// Copyright (c) 2009-2016 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.


// Maintainer: joaander

#include "TwoStepBDGPU.h"
#include "TwoStepBDGPU.cuh"

#ifdef ENABLE_MPI
#include "hoomd/HOOMDMPI.h"
#endif

#include <boost/python.hpp>
using namespace boost::python;
#include <boost/bind.hpp>
using namespace boost;

using namespace std;

/*! \file TwoStepBDGPU.h
    \brief Contains code for the TwoStepBDGPU class
*/

/*! \param sysdef SystemDefinition this method will act on. Must not be NULL.
    \param group The group of particles this integration method is to work on
    \param T Temperature set point as a function of time
    \param seed Random seed to use in generating random numbers
    \param use_lambda If true, gamma=lambda*diameter, otherwise use a per-type gamma via setGamma()
    \param lambda Scale factor to convert diameter to gamma
*/
TwoStepBDGPU::TwoStepBDGPU(std::shared_ptr<SystemDefinition> sysdef,
                           std::shared_ptr<ParticleGroup> group,
                           std::shared_ptr<Variant> T,
                           unsigned int seed,
                           bool use_lambda,
                           Scalar lambda,
                           bool noiseless_t,
                           bool noiseless_r)
    : TwoStepBD(sysdef, group, T, seed, use_lambda, lambda, noiseless_t, noiseless_r)
    {
    if (!m_exec_conf->isCUDAEnabled())
        {
        m_exec_conf->msg->error() << "Creating a TwoStepBDGPU while CUDA is disabled" << endl;
        throw std::runtime_error("Error initializing TwoStepBDGPU");
        }

    m_block_size = 256;
    }

/*! \param timestep Current time step
    \post Particle positions are moved forward a full time step and velocities are redrawn from the proper distrubtion.
*/
void TwoStepBDGPU::integrateStepOne(unsigned int timestep)
    {
    // profile this step
    if (m_prof)
        m_prof->push(m_exec_conf, "BD step 1");

    // access all the needed data
    BoxDim box = m_pdata->getBox();
    ArrayHandle< unsigned int > d_index_array(m_group->getIndexArray(), access_location::device, access_mode::read);
    unsigned int group_size = m_group->getNumMembers();
    const unsigned int D = m_sysdef->getNDimensions();
    const GPUArray< Scalar4 >& net_force = m_pdata->getNetForce();

    ArrayHandle<Scalar4> d_pos(m_pdata->getPositions(), access_location::device, access_mode::readwrite);
    ArrayHandle<Scalar4> d_vel(m_pdata->getVelocities(), access_location::device, access_mode::readwrite);
    ArrayHandle<int3> d_image(m_pdata->getImages(), access_location::device, access_mode::readwrite);

    ArrayHandle<Scalar4> d_net_force(net_force, access_location::device, access_mode::read);
    ArrayHandle<Scalar> d_gamma(m_gamma, access_location::device, access_mode::read);
    ArrayHandle<Scalar> d_diameter(m_pdata->getDiameters(), access_location::device, access_mode::read);
    ArrayHandle<unsigned int> d_tag(m_pdata->getTags(), access_location::device, access_mode::read);

    // for rotational noise
    ArrayHandle<Scalar> d_gamma_r(m_gamma_r, access_location::device, access_mode::read);
    ArrayHandle<Scalar4> d_orientation(m_pdata->getOrientationArray(), access_location::device, access_mode::readwrite);
    ArrayHandle<Scalar4> d_torque(m_pdata->getNetTorqueArray(), access_location::device, access_mode::readwrite);
    ArrayHandle<Scalar3> d_inertia(m_pdata->getMomentsOfInertiaArray(), access_location::device, access_mode::read);
    ArrayHandle<Scalar4> d_angmom(m_pdata->getAngularMomentumArray(), access_location::device, access_mode::readwrite);

    unsigned int num_blocks = group_size / m_block_size + 1;

    langevin_step_two_args args;
    args.d_gamma = d_gamma.data;
    args.n_types = m_gamma.getNumElements();
    args.use_lambda = m_use_lambda;
    args.lambda = m_lambda;
    args.T = m_T->getValue(timestep);
    args.timestep = timestep;
    args.seed = m_seed;
    args.d_sum_bdenergy = NULL;
    args.d_partial_sum_bdenergy = NULL;
    args.block_size = m_block_size;
    args.num_blocks = num_blocks;
    args.tally = false;

    bool aniso = m_aniso;

    // perform the update on the GPU
    gpu_brownian_step_one(d_pos.data,
                          d_vel.data,
                          d_image.data,
                          box,
                          d_diameter.data,
                          d_tag.data,
                          d_index_array.data,
                          group_size,
                          d_net_force.data,
                          d_gamma_r.data,
                          d_orientation.data,
                          d_torque.data,
                          d_inertia.data,
                          d_angmom.data,
                          args,
                          aniso,
                          m_deltaT,
                          D,
                          m_noiseless_t,
                          m_noiseless_r);

    if(m_exec_conf->isCUDAErrorCheckingEnabled())
        CHECK_CUDA_ERROR();

    // done profiling
    if (m_prof)
        m_prof->pop(m_exec_conf);
    }

/*! \param timestep Current time step
    \post particle velocities are moved forward to timestep+1 on the GPU
*/
void TwoStepBDGPU::integrateStepTwo(unsigned int timestep)
    {
    // there is no step 2
    }

void export_TwoStepBDGPU()
    {
    class_<TwoStepBDGPU, std::shared_ptr<TwoStepBDGPU>, bases<TwoStepBD>, boost::noncopyable>
        ("TwoStepBDGPU", init< std::shared_ptr<SystemDefinition>,
                               std::shared_ptr<ParticleGroup>,
                               std::shared_ptr<Variant>,
                               unsigned int,
                               bool,
                               Scalar,
                               bool,
                               bool>())
        ;
    }
