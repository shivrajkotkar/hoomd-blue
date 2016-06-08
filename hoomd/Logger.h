// Copyright (c) 2009-2016 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.


// Maintainer: joaander

/*! \file Logger.h
    \brief Declares the Logger class
*/

#ifdef NVCC
#error This header cannot be compiled by nvcc
#endif

#include "Analyzer.h"
#include "ClockSource.h"
#include "Compute.h"
#include "Updater.h"

#include <string>
#include <vector>
#include <map>
#include <fstream>

#include <memory>

#ifndef __LOGGER_H__
#define __LOGGER_H__

//! Logs registered quantities to a delimited file
/*! \note design notes: Computes and Updaters have getProvidedLogQuantities and getLogValue. The first lists
    all quantities that the compute/updater provides (a list of strings). And getLogValue takes a string
    as an argument and returns a scalar.

    Logger will open and overwrite its log file on construction. Any number of computes and updaters
    can be registered with the Logger. It will track which quantities are provided. If any particular
    quantity is registered twice, a warning is printed and the most recent registered source will take
    effect. setLoggedQuantities will specify a list of quantities to log. When it is called, a header
    is written to the file. Every call to analyze() will result in the computes for the logged quantities
    being called and getLogValue called for each value to produce a line in the file. If a logged quantity
    is not registered, a 0 is printed to the file and a warning to stdout.

    The removeAll method can be used to clear all registered computes and updaters. hoomd_script will
    removeAll() and re-register all active computes and updaters before every run()

    As an option, Logger can be initialized with no file. Such a logger will skip doing anything during
    analyze() but is still available for getQuantity() operations.

    \ingroup analyzers
*/
class Logger : public Analyzer
    {
    public:
        //! Constructs a logger and opens the file
        Logger(std::shared_ptr<SystemDefinition> sysdef,
               const std::string& fname,
               const std::string& header_prefix="",
               bool overwrite=false);

        //! Destructor
        ~Logger();

        //! Registers a compute
        void registerCompute(std::shared_ptr<Compute> compute);

        //! Registers an updater
        void registerUpdater(std::shared_ptr<Updater> updater);

        //! Register a callback
        void registerCallback(std::string name, boost::python::object callback);

        //! Clears all registered computes and updaters
        void removeAll();

        //! Selects which quantities to log
        void setLoggedQuantities(const std::vector< std::string >& quantities);

        //! Sets the delimiter to use between fields
        void setDelimiter(const std::string& delimiter);

        //! Query the current value for a given quantity
        Scalar getQuantity(const std::string& quantity, unsigned int timestep, bool use_cache);

        //! Write out the data for the current timestep
        void analyze(unsigned int timestep);

        //! Get needed pdata flags
        /*! Logger may potentially log any of the optional quantities, enable all of the bits.
        */
        virtual PDataFlags getRequestedPDataFlags()
            {
            PDataFlags flags;
            flags[pdata_flag::isotropic_virial] = 1;
            flags[pdata_flag::potential_energy] = 1;
            flags[pdata_flag::pressure_tensor] = 1;
            flags[pdata_flag::rotational_kinetic_energy] = 1;
            return flags;
            }

    private:
        //! The delimiter to put between columns in the file
        std::string m_delimiter;
        //! The output file name
        std::string m_filename;
        //! The prefix written at the beginning of the header line
        std::string m_header_prefix;
        //! Flag indicating this file is being appended to
        bool m_appending;
        //! The file we write out to
        std::ofstream m_file;
        //! A map of computes indexed by logged quantity that they provide
        std::map< std::string, std::shared_ptr<Compute> > m_compute_quantities;
        //! A map of updaters indexed by logged quantity that they provide
        std::map< std::string, std::shared_ptr<Updater> > m_updater_quantities;
        //! List of callbacks
        std::map< std::string, boost::python::object > m_callback_quantities;
        //! List of quantities to log
        std::vector< std::string > m_logged_quantities;
        //! Clock for the time log quantity
        ClockSource m_clk;
        //! The number of the last timestep when quantities were computed.
        unsigned int m_cached_timestep;
        //! The values of the logged quantities at the last logger update.
        std::vector< Scalar > m_cached_quantities;
        //! Flag to indicate whether we have initialized the file IO
        bool m_is_initialized;
        //! true if we are writing to the output file
        bool m_file_output;

        //! Helper function to get a value for a given quantity
        Scalar getValue(const std::string &quantity, int timestep);

        //! Helper function to open output files
        void openOutputFiles();
    };

//! exports the Logger class to python
void export_Logger();

#endif
