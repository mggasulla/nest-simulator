/*
 *  iaf_psc_exp_multisynapse.cpp
 *
 *  This file is part of NEST.
 *
 *  Copyright (C) 2004 The NEST Initiative
 *
 *  NEST is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  NEST is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with NEST.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "iaf_psc_exp_multisynapse.h"

// C++ includes:
#include <limits>

// Includes from libnestutil:
#include "numerics.h"
#include "propagator_stability.h"

// Includes from nestkernel:
#include "exceptions.h"
#include "kernel_manager.h"
#include "universal_data_logger_impl.h"

// Includes from sli:
#include "dict.h"
#include "dictutils.h"
#include "doubledatum.h"
#include "integerdatum.h"

/* ----------------------------------------------------------------
 * Recordables map
 * ---------------------------------------------------------------- */

nest::RecordablesMap< nest::iaf_psc_exp_multisynapse >
  nest::iaf_psc_exp_multisynapse::recordablesMap_;

namespace nest
{
// Override the create() method with one call to RecordablesMap::insert_()
// for each quantity to be recorded.
template <>
void
RecordablesMap< iaf_psc_exp_multisynapse >::create()
{
  // use standard names whereever you can for consistency!
  insert_( names::V_m, &iaf_psc_exp_multisynapse::get_V_m_ );
}


/* ----------------------------------------------------------------
 * Default constructors defining default parameters and state
 * ---------------------------------------------------------------- */

iaf_psc_exp_multisynapse::Parameters_::Parameters_()
  : Tau_( 10.0 )             // in ms
  , C_( 250.0 )              // in pF
  , t_ref_( 2.0 )            // in ms
  , E_L_( -70.0 )            // in mV
  , I_e_( 0.0 )              // in pA
  , V_reset_( -70.0 - E_L_ ) // in mV
  , Theta_( -55.0 - E_L_ )   // relative E_L_
  , num_of_receptors_( 0 )
  , has_connections_( false )
{
  tau_syn_.clear();
}

iaf_psc_exp_multisynapse::State_::State_()
  : I_const_( 0.0 )
  , V_m_( 0.0 )
  , refractory_steps_( 0 )
{
  i_syn_.clear();
}

/* ----------------------------------------------------------------
 * Parameter and state extractions and manipulation functions
 * ---------------------------------------------------------------- */

void
iaf_psc_exp_multisynapse::Parameters_::get( DictionaryDatum& d ) const
{
  def< double >( d, names::E_L, E_L_ ); // resting potential
  def< double >( d, names::I_e, I_e_ );
  def< double >( d, names::V_th, Theta_ + E_L_ ); // threshold value
  def< double >( d, names::V_reset, V_reset_ + E_L_ );
  def< double >( d, names::C_m, C_ );
  def< double >( d, names::tau_m, Tau_ );
  def< double >( d, names::t_ref, t_ref_ );
  def< int >( d, names::n_synapses, num_of_receptors_ );
  def< bool >( d, names::has_connections, has_connections_ );

  ArrayDatum tau_syn_ad( tau_syn_ );
  def< ArrayDatum >( d, names::tau_syn, tau_syn_ad );
}

double
iaf_psc_exp_multisynapse::Parameters_::set( const DictionaryDatum& d )
{
  // if E_L_ is changed, we need to adjust all variables defined relative to
  // E_L_
  const double ELold = E_L_;
  updateValue< double >( d, names::E_L, E_L_ );
  const double delta_EL = E_L_ - ELold;

  if ( updateValue< double >( d, names::V_reset, V_reset_ ) )
  {
    V_reset_ -= E_L_;
  }
  else
  {
    V_reset_ -= delta_EL;
  }
  if ( updateValue< double >( d, names::V_th, Theta_ ) )
  {
    Theta_ -= E_L_;
  }
  else
  {
    Theta_ -= delta_EL;
  }
  
  updateValue< double >( d, names::I_e, I_e_ );
  updateValue< double >( d, names::C_m, C_ );
  updateValue< double >( d, names::tau_m, Tau_ );
  updateValue< double >( d, names::t_ref, t_ref_ );

  if ( C_ <= 0 )
  {
    throw BadProperty( "Capacitance must be > 0." );
  }
  if ( Tau_ <= 0. )
  {
    throw BadProperty( "Membrane time constant must be strictly positive." );
  }
  std::vector< double > tau_tmp;
  if ( updateValue< std::vector< double > >( d, "tau_syn", tau_tmp ) )
  {
    for ( size_t i = 0; i < tau_tmp.size(); ++i )
    {
      if ( tau_tmp.size() < tau_syn_.size() && has_connections_ == true )
      {
        throw BadProperty(
          "The neuron has connections, therefore the number of ports cannot be "
          "reduced." );
      }
      if ( tau_tmp[ i ] <= 0 )
      {
        throw BadProperty( 
                     "All synaptic time constants must be strictly positive." );
      }
      if ( tau_tmp[ i ] == Tau_ )
      {
        throw BadProperty(
          "Membrane and synapse time constant(s) must differ. See note in "
          "documentation." );
      }
    }

    tau_syn_ = tau_tmp;
    num_of_receptors_ = tau_syn_.size();
  }

  if ( t_ref_ < 0. )
  {
    throw BadProperty( "Refractory time must not be negative." );
  }
  if ( V_reset_ >= Theta_ )
  {
    throw BadProperty( "Reset potential must be smaller than threshold." );
  }
  return delta_EL;
}

void
iaf_psc_exp_multisynapse::State_::get( DictionaryDatum& d,
  const Parameters_& p ) const
{
  def< double >( d, names::V_m, V_m_ + p.E_L_ ); // Membrane potential
}

void
iaf_psc_exp_multisynapse::State_::set( const DictionaryDatum& d,
                                       const Parameters_& p,
                                       double delta_EL )
{
  if ( updateValue< double >( d, names::V_m, V_m_ ) )
  {
    V_m_ -= p.E_L_;
  }
  else
  {
    V_m_ -= delta_EL;
  }
}

iaf_psc_exp_multisynapse::Buffers_::Buffers_( iaf_psc_exp_multisynapse& n )
  : logger_( n )
{
}

iaf_psc_exp_multisynapse::Buffers_::Buffers_( const Buffers_&,
  iaf_psc_exp_multisynapse& n )
  : logger_( n )
{
}

/* ----------------------------------------------------------------
 * Default and copy constructor for node
 * ---------------------------------------------------------------- */

iaf_psc_exp_multisynapse::iaf_psc_exp_multisynapse()
  : Archiving_Node()
  , P_()
  , S_()
  , B_( *this )
{
  recordablesMap_.create();
}

iaf_psc_exp_multisynapse::iaf_psc_exp_multisynapse(
  const iaf_psc_exp_multisynapse& n )
  : Archiving_Node( n )
  , P_( n.P_ )
  , S_( n.S_ )
  , B_( n.B_, *this )
{
}

/* ----------------------------------------------------------------
 * Node initialization functions
 * ---------------------------------------------------------------- */

void
iaf_psc_exp_multisynapse::init_state_( const Node& proto )
{
  const iaf_psc_exp_multisynapse& pr =
    downcast< iaf_psc_exp_multisynapse >( proto );
  S_ = pr.S_;
}

void
iaf_psc_exp_multisynapse::init_buffers_()
{
  B_.spikes_.clear();   // includes resize
  B_.currents_.clear(); // includes resize

  B_.logger_.reset();

  Archiving_Node::clear_history();
}

void
nest::iaf_psc_exp_multisynapse::calibrate()
{
  // ensures initialization in case mm connected after Simulate
  B_.logger_.init();

  const double h = Time::get_resolution().get_ms();

  P_.receptor_types_.resize( P_.num_of_receptors_ );
  for ( size_t i = 0; i < P_.num_of_receptors_; i++ )
  {
    P_.receptor_types_[ i ] = i + 1;
  }

  V_.P11_syn_.resize( P_.num_of_receptors_ );
  V_.P21_syn_.resize( P_.num_of_receptors_ );

  S_.i_syn_.resize( P_.num_of_receptors_ );

  B_.spikes_.resize( P_.num_of_receptors_ );

  V_.P22_ = std::exp( -h / P_.Tau_ );
  V_.P20_ = P_.Tau_ / P_.C_ * ( 1.0 - V_.P22_ );

  for ( size_t i = 0; i < P_.num_of_receptors_; i++ )
  {
    V_.P11_syn_[ i ] = std::exp( -h / P_.tau_syn_[ i ] );
    // these are determined according to a numeric stability criterion
    V_.P21_syn_[ i ] = propagator_32( P_.tau_syn_[ i ], P_.Tau_, P_.C_, h );

    B_.spikes_[ i ].resize();
  }

  V_.RefractoryCounts_ = Time( Time::ms( P_.t_ref_ ) ).get_steps();
}

void
iaf_psc_exp_multisynapse::update( const Time& origin,
  const long from,
  const long to )
{
  assert(
    to >= 0 && ( delay ) from < kernel().connection_manager.get_min_delay() );
  assert( from < to );

  // evolve from timestep 'from' to timestep 'to' with steps of h each
  for ( long lag = from; lag < to; ++lag )
  {
    if ( S_.refractory_steps_ == 0 ) // neuron not refractory, so evolve V
    {
      S_.V_m_ = S_.V_m_ * V_.P22_
        + ( P_.I_e_ + S_.I_const_ ) * V_.P20_; // not sure about this

      S_.current_ = 0.0;
      for ( size_t i = 0; i < P_.num_of_receptors_; i++ )
      {
        S_.V_m_ += V_.P21_syn_[ i ] * S_.i_syn_[ i ];
        S_.current_ += S_.i_syn_[ i ]; // not sure about this
      }
    }
    else
    {
      --S_.refractory_steps_; // neuron is absolute refractory
    }
    for ( size_t i = 0; i < P_.num_of_receptors_; i++ )
    {
      // exponential decaying PSCs
      S_.i_syn_[ i ] *= V_.P11_syn_[ i ];

      // collect spikes
      S_.i_syn_[ i ] += B_.spikes_[ i ].get_value( lag ); // not sure about this
    }

    if ( S_.V_m_ >= P_.Theta_ ) // threshold crossing
    {
      S_.refractory_steps_ = V_.RefractoryCounts_;
      S_.V_m_ = P_.V_reset_;

      set_spiketime( Time::step( origin.get_steps() + lag + 1 ) );
      SpikeEvent se;
      kernel().event_delivery_manager.send( *this, se, lag );
    }

    // set new input current
    S_.I_const_ = B_.currents_.get_value( lag );

    // log state data
    B_.logger_.record_data( origin.get_steps() + lag );
  }
}

port
iaf_psc_exp_multisynapse::handles_test_event( SpikeEvent&, rport receptor_type )
{
  if ( receptor_type <= 0
    || receptor_type > static_cast< port >( P_.num_of_receptors_ ) )
  {
    throw IncompatibleReceptorType( receptor_type, get_name(), "SpikeEvent" );
  }
  
  P_.has_connections_ = true;
  return receptor_type;
}

void
iaf_psc_exp_multisynapse::handle( SpikeEvent& e )
{
  assert( e.get_delay() > 0 );

  B_.spikes_[ e.get_rport() - 1 ].add_value(
  e.get_rel_delivery_steps(
    kernel().simulation_manager.get_slice_origin() ),
    e.get_weight() * e.get_multiplicity() );
}

void
iaf_psc_exp_multisynapse::handle( CurrentEvent& e )
{
  assert( e.get_delay() > 0 );

  const double I = e.get_current();
  const double w = e.get_weight();

  // add weighted current; HEP 2002-10-04
  B_.currents_.add_value(
    e.get_rel_delivery_steps( kernel().simulation_manager.get_slice_origin() ),
    w * I );
}

void
iaf_psc_exp_multisynapse::handle( DataLoggingRequest& e )
{
  B_.logger_.handle( e );
}

} // namespace
