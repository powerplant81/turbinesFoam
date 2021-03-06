/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2011-2014 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

Class
    Foam::fv::crossFlowTurbineALSource

Description
    Cell based momentum source

    Source approximates the mean effects of rotor forces on a cylindrical
    region within the domain

    \heading Source usage

    Example usage:
    \verbatim
    crossFlowTurbineALSourceCoeffs
    {
        fieldNames      (U);    // names of fields on which to apply source
        nBlades         3;      // number of blades
        tipEffect       0.96;   // normalised radius above which lift = 0

        inletFlowType   local;  // inlet flow type specification

        geometryMode    auto;   // geometry specification

        trimModel       fixed;  // fixed || targetForce

        flapCoeffs
        {
            beta0           0;  // coning angle [deg]
            beta1c          0;  // lateral flapping coeff (cos coeff)
            beta2s          0;  // longitudinal flapping coeff (sin coeff)
        }

        blade
        {
            ...
        }

        profiles
        {
            ...
        }
    }
    \endverbatim

    Where:
    Valid options for the \c geometryMode entry include:
    - auto          : determine rototor co-ord system from cells
    - specified     : specified co-ord system

    Valid options for the \c inletFlowType entry include:
    - fixed         : specified velocity
    - local         : use local flow conditions
    - surfaceNormal : specified normal velocity (positive towards rotor)


SourceFiles
    crossFlowTurbineALSource.C
    crossFlowTurbineALSourceTemplates.C

\*---------------------------------------------------------------------------*/

#ifndef crossFlowTurbineALSource_H
#define crossFlowTurbineALSource_H

#include "fvOption.H"
#include "cylindricalCS.H"
#include "localAxesRotation.H"
#include "NamedEnum.H"
#include "actuatorLineSource.H"
#include "volFieldsFwd.H"
#include "OFstream.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace fv
{

/*---------------------------------------------------------------------------*\
                      Class crossFlowTurbineALSource Declaration
\*---------------------------------------------------------------------------*/

class crossFlowTurbineALSource
:
    public option
{
public:


protected:

    // Protected data
    
        //- Runtime pointer
        const Time& time_;
        
        //- Time value used to determine whether to rotate
        scalar lastRotationTime_;
        
        //- Origin of turbine coordinate system
        vector origin_;
        
        //- Turbine axis of rotation
        vector axis_;

        //- Reference density for incompressible case
        scalar rhoRef_;

        //- Rotational speed in rad/s
        //  Positive anti-clockwise when looking along axis direction
        scalar omega_;
        
        //- Azimuthal angle in degrees (does not reset each rotation)
        scalar angleDeg_;

        //- Number of blades
        label nBlades_;
        
        //- Switch for struts
        bool hasStruts_;
        
        //- Switch for shaft
        bool hasShaft_;

        //- Inlet velocity for specified inflow
        vector freeStreamVelocity_;
        
        //- Free stream velocity direction
        vector freeStreamDirection_;
        
        //- Radial direction
        vector radialDirection_;
        
        //- Upstream point to determine free stream velocity
        vector upstreamPoint_;
        
        //- Rotor tip speed ratio
        scalar tipSpeedRatio_;
        
        //- Rotor radius for calculating tip speed ratio
        scalar rotorRadius_;

        //- Tip effect [0-1]
        //  Ratio of blade radius beyond which lift=0
        scalar tipEffect_;

        //- List of actuator lines as blades
        PtrList<actuatorLineSource> blades_;
        
        //- List of actuator lines as struts
        PtrList<actuatorLineSource> struts_;
        
        //- Actuator line for shaft
        autoPtr<actuatorLineSource> shaft_;
        
        //- Blade definitions dictionary
        dictionary bladesDict_;
        
        //- List of blade names
        wordList bladeNames_;
        
        //- Strut definitions dictionary
        dictionary strutsDict_;
        
        //- Shaft dictionary
        dictionary shaftDict_;
        
        //- Dictionary for profile coefficient data
        dictionary profilesDict_;
        
        //- Force vector
        vector force_;
        
        //- Force field (per unit density)
        volVectorField forceField_;
        
        //- Torque about the axis
        scalar torque_;
        
        //- Frontal area in m^2
        scalar frontalArea_;
        
        //- Nondimensional power coefficient
        scalar powerCoefficient_;
        
        //- Nondimensional drag coefficient
        scalar dragCoefficient_;
        
        //- Nondimensional torque coefficient
        scalar torqueCoefficient_;
        
        //- Output file stream
        OFstream* outputFile_;


    // Protected Member Functions
    
        //- Rotate a vector
        void rotateVector
        (
            vector& vectorToRotate,
            vector rotationPoint,
            vector axis,
            scalar radians
        );
        
        //- Create actuator lines for blades
        void createBlades();
        
        //- Create actuator lines for struts
        void createStruts();
        
        //- Create actuator line for shaft
        void createShaft();

        //- Create the co-ordinate system
        void createCoordinateSystem();

        //- Return the inlet flow field
        tmp<vectorField> inflowVelocity(const volVectorField& U) const;
        
        //- Rotate the turbine
        void rotate();


public:

    //- Runtime type information
    TypeName("crossFlowTurbineALSource");


    // Constructors


        //- Construct from components
        crossFlowTurbineALSource
        (
            const word& name,
            const word& modelType,
            const dictionary& dict,
            const fvMesh& mesh
        );


    //- Destructor
    virtual ~crossFlowTurbineALSource();


    // Member Functions

        // Access
        
            //- Return const access to runTime
            inline const Time& time() const;

            //- Return the reference density for incompressible case
            inline scalar rhoRef() const;

            //- Return the rotational speed [rad/s]
            //  Positive anti-clockwise when looking along -ve lift direction
            inline scalar omega() const;
            
            //- Return force vector
            vector& force();
            
            //- Return force field
            volVectorField& forceField();
            
            //- Return torque
            scalar& torque();

        // Source term addition

            //- Source term to momentum equation
            virtual void addSup
            (
                fvMatrix<vector>& eqn,
                const label fieldI
            );

            //- Source term to compressible momentum equation
            virtual void addSup
            (
                const volScalarField& rho,
                fvMatrix<vector>& eqn,
                const label fieldI
            );


        // I-O
        
            //- Create the turbine output file
            virtual void createOutputFile();

            //- Write the turbine performance to file
            virtual void writePerf();
            
            //- Write the source properties
            virtual void writeData(Ostream&) const;

            //- Read source dictionary
            virtual bool read(const dictionary& dict);
            
            //- Print dictionary values
            virtual void printCoeffs() const;
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace fv
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#include "crossFlowTurbineALSourceI.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
