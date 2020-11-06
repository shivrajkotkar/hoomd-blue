# Copyright (c) 2009-2019 The Regents of the University of Michigan
# This file is part of the HOOMD-blue project, released under the BSD 3-Clause
# License.

"""Test hoomd.hpmc.update.BoxMC."""

import hoomd
import pytest
import math
from itertools import product

valid_constructor_args = [
    dict(trigger=hoomd.trigger.Periodic(10),
         betaP=hoomd.variant.Constant(10),
         seed=1),
    dict(trigger=hoomd.trigger.After(100),
         betaP=hoomd.variant.Ramp(1, 5, 0, 100),
         seed=4),
    dict(trigger=hoomd.trigger.Before(100),
         betaP=hoomd.variant.Cycle(1, 5, 0, 10, 20, 10, 15),
         seed=87),
    dict(trigger=hoomd.trigger.Periodic(1000),
         betaP=hoomd.variant.Power(1, 5, 3, 0, 100),
         seed=4),
]

valid_attrs = [
    ('betaP', hoomd.variant.Constant(10)),
    ('betaP', hoomd.variant.Ramp(1, 5, 0, 100)),
    ('betaP', hoomd.variant.Cycle(1, 5, 0, 10, 20, 10, 15)),
    ('betaP', hoomd.variant.Power(1, 5, 3, 0, 100)),
    ('volume', {"weight": 0.7, "delta":0.3}),
    ('ln_volume', {"weight": 0.1, "delta": 1.2}),
    ('aspect', {"weight": 0.3, "delta": 0.1}),
    ('length', {"weight": 0.5, "delta": [0.8]*3}),
    ('shear', {"weight": 0.7, "delta": [0.3]*3, "reduce": 0.1})
]

betaP_boxmoves = list(product([1, 3, 5, 7, 10, 20],
                              ["volume", "ln_volume", "aspect",
                               "length", "shear"]))


@pytest.mark.parametrize("constructor_args", valid_constructor_args)
def test_valid_construction(constructor_args):
    """Test that BoxMC can be constructed with valid arguments."""
    boxmc = hoomd.hpmc.update.BoxMC(**constructor_args)

    # validate the params were set properly
    for attr, value in constructor_args.items():
        assert getattr(boxmc, attr) == value


@pytest.mark.parametrize("constructor_args", valid_constructor_args)
def test_valid_construction_and_attach(simulation_factory,
                                       two_particle_snapshot_factory,
                                       constructor_args):
    """Test that BoxMC can be attached with valid arguments."""
    boxmc = hoomd.hpmc.update.BoxMC(**constructor_args)

    sim = simulation_factory(two_particle_snapshot_factory())
    sim.operations.updaters.append(boxmc)

    # BoxMC requires an HPMC integrator
    mc = hoomd.hpmc.integrate.Sphere(seed=1)
    mc.shape['A'] = dict(diameter=1)
    sim.operations.integrator = mc

    sim.operations._schedule()

    # validate the params were set properly
    for attr, value in constructor_args.items():
        assert getattr(boxmc, attr) == value


@pytest.mark.parametrize("attr,value", valid_attrs)
def test_valid_setattr(attr, value):
    """Test that BoxMC can get and set attributes."""
    boxmc = hoomd.hpmc.update.BoxMC(trigger=hoomd.trigger.Periodic(10),
                                    betaP=10,
                                    seed=1)

    setattr(boxmc, attr, value)
    assert getattr(boxmc, attr) == value


@pytest.mark.parametrize("attr,value", valid_attrs)
def test_valid_setattr_attached(attr, value, simulation_factory,
                                two_particle_snapshot_factory):
    """Test that BoxMC can get and set attributes while attached."""
    boxmc = hoomd.hpmc.update.BoxMC(trigger=hoomd.trigger.Periodic(10),
                                    betaP=10,
                                    seed=1)

    sim = simulation_factory(two_particle_snapshot_factory())
    sim.operations.updaters.append(boxmc)

    # BoxMC requires an HPMC integrator
    mc = hoomd.hpmc.integrate.Sphere(seed=1)
    mc.shape['A'] = dict(diameter=1)
    sim.operations.integrator = mc

    sim.operations._schedule()

    setattr(boxmc, attr, value)
    assert getattr(boxmc, attr) == value


@pytest.mark.parametrize("betaP,box_move", betaP_boxmoves)
@pytest.mark.validate
def test_sphere_compression(betaP, box_move, simulation_factory,
                            lattice_snapshot_factory):
    """Test that BoxMC can compress (and expand) simulation boxes."""
    n = 7
    snap = lattice_snapshot_factory(n=n, a=1.1)

    boxmc = hoomd.hpmc.update.BoxMC(trigger=hoomd.trigger.Periodic(10),
                                    betaP=hoomd.variant.Constant(betaP),
                                    seed=1)

    sim = simulation_factory(snap)
    initial_box = sim.state.box

    sim.operations.updaters.append(boxmc)
    mc = hoomd.hpmc.integrate.Sphere(d=0.05, seed=1)
    mc.shape['A'] = dict(diameter=1)
    sim.operations.integrator = mc

    # run w/o setting any of the box moves
    sim.run(100)

    # check that the box remains unchanged
    assert mc.overlaps == 0
    assert sim.state.box == initial_box

    # add a box move
    delta = [0.2]*3 if box_move in ("shear", "length") else 0.2
    setattr(boxmc, box_move, {"weight": 1, "delta": delta})
    sim.run(100)

    # check that box is changed
    assert mc.overlaps == 0
    assert sim.state.box != initial_box


@pytest.mark.parametrize("betaP,box_move", betaP_boxmoves)
@pytest.mark.validate
def test_disk_compression(betaP, box_move, simulation_factory,
                          lattice_snapshot_factory):
    """Test that BoxMC can compress (and expand) simulation boxes."""
    n = 7
    snap = lattice_snapshot_factory(dimensions=2, n=n, a=1.1)

    boxmc = hoomd.hpmc.update.BoxMC(trigger=hoomd.trigger.Periodic(10),
                                    betaP=hoomd.variant.Constant(betaP),
                                    seed=1)

    sim = simulation_factory(snap)
    initial_box = sim.state.box

    sim.operations.updaters.append(boxmc)
    mc = hoomd.hpmc.integrate.Sphere(d=0.05, seed=1)
    mc.shape['A'] = dict(diameter=1)
    sim.operations.integrator = mc

    # run w/o setting any of the box moves
    sim.run(100)

    # check that the box remains unchanged
    assert mc.overlaps == 0
    assert sim.state.box == initial_box

    # add a box move
    delta = [0.2]*3 if box_move in ("shear", "length") else 0.2
    setattr(boxmc, box_move, {"weight": 1, "delta": delta})
    sim.run(100)

    # check that box is changed
    assert mc.overlaps == 0
    assert sim.state.box != initial_box