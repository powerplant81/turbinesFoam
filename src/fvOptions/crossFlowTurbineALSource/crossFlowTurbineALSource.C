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

\*---------------------------------------------------------------------------*/

#include "crossFlowTurbineALSource.H"
#include "addToRunTimeSelectionTable.H"
#include "fvMatrices.H"
#include "geometricOneField.H"
#include "syncTools.H"

using namespace Foam::constant;

// * * * * * * * * * * * * * Static Member Functions * * * * * * * * * * * * //

namespace Foam
{
namespace fv
{
    defineTypeNameAndDebug(crossFlowTurbineALSource, 0);
    addToRunTimeSelectionTable
    (
        option,
        crossFlowTurbineALSource,
        dictionary
    );
}
}


// * * * * * * * * * * * * Protected Member Functions  * * * * * * * * * * * //

void Foam::fv::crossFlowTurbineALSource::rotateVector
(
    vector& vectorToRotate,
    vector rotationPoint, 
    vector axis,
    scalar radians
)
{
    // Declare and define the rotation matrix (from SOWFA)
    tensor RM;
    scalar angle = radians;
    RM.xx() = Foam::sqr(axis.x()) + (1.0 - Foam::sqr(axis.x())) * Foam::cos(angle); 
    RM.xy() = axis.x() * axis.y() * (1.0 - Foam::cos(angle)) - axis.z() * Foam::sin(angle); 
    RM.xz() = axis.x() * axis.z() * (1.0 - Foam::cos(angle)) + axis.y() * Foam::sin(angle);
    RM.yx() = axis.x() * axis.y() * (1.0 - Foam::cos(angle)) + axis.z() * Foam::sin(angle); 
    RM.yy() = Foam::sqr(axis.y()) + (1.0 - Foam::sqr(axis.y())) * Foam::cos(angle);
    RM.yz() = axis.y() * axis.z() * (1.0 - Foam::cos(angle)) - axis.x() * Foam::sin(angle);
    RM.zx() = axis.x() * axis.z() * (1.0 - Foam::cos(angle)) - axis.y() * Foam::sin(angle);
    RM.zy() = axis.y() * axis.z() * (1.0 - Foam::cos(angle)) + axis.x() * Foam::sin(angle);
    RM.zz() = Foam::sqr(axis.z()) + (1.0 - Foam::sqr(axis.z())) * Foam::cos(angle);
    
    // Rotation matrices make a rotation about the origin, so need to subtract 
    // rotation point off the point to be rotated.
    vectorToRotate -= rotationPoint;

    // Perform the rotation.
    vectorToRotate = RM & vectorToRotate;

    // Return the rotated point to its new location relative to the rotation point.
    vectorToRotate += rotationPoint;
}


void Foam::fv::crossFlowTurbineALSource::createCoordinateSystem()
{
    // Construct the local rotor coordinate system
    freeStreamDirection_ = freeStreamVelocity_/mag(freeStreamVelocity_);
    radialDirection_ = axis_^freeStreamDirection_;
    radialDirection_ = radialDirection_/mag(radialDirection_);
    // Make sure axis is a unit vector
    axis_ /= mag(axis_);
}


Foam::tmp<Foam::vectorField> Foam::fv::crossFlowTurbineALSource::inflowVelocity
(
    const volVectorField& U
) const
{
    return U.internalField();
}


void Foam::fv::crossFlowTurbineALSource::createBlades()
{
    int nBlades = nBlades_;
    blades_.setSize(nBlades);
    int nElements;
    word profileName;
    List<List<scalar> > elementData;
    List<List<scalar> > profileData;
    word modelType = "actuatorLineSource";
    List<scalar> frontalAreas(nBlades); // frontal area from each blade
    
    for (int i = 0; i < nBlades_; i++)
    {
        word& bladeName = bladeNames_[i];
        // Create dictionary items for this blade
        dictionary bladeSubDict;
        bladeSubDict = bladesDict_.subDict(bladeName);
        bladeSubDict.lookup("nElements") >> nElements;
        bladeSubDict.lookup("profile") >> profileName;
        bladeSubDict.lookup("elementData") >> elementData;
        profilesDict_.subDict(profileName).lookup("data") >> profileData;
        
        bladeSubDict.add("freeStreamVelocity", freeStreamVelocity_);
        bladeSubDict.add("fieldNames", coeffs_.lookup("fieldNames"));
        bladeSubDict.add("coefficientData", profileData);
        bladeSubDict.add("tipEffect", tipEffect_);
        
        if (debug)
        {
            Info<< "Creating actuator line blade " << bladeName << endl;
            Info<< "Blade has " << nElements << " elements" << endl;
            Info<< "Blade profile: " << profileName << endl;
            Info<< "Element data:" << endl;
            Info<< elementData << endl << endl;
            Info<< "Profile sectional coefficient data:" << endl;
            Info<< profileData << endl << endl;
        }
        
        // Convert element data into actuator line element geometry
        label nGeomPoints = elementData.size();
        List<List<List<scalar> > > elementGeometry(nGeomPoints);
        List<vector> initialVelocities(nGeomPoints, vector::one);
        // Frontal area for this blade
        scalar frontalArea = 0.0;
        forAll(elementData, j)
        {
            // Read CFTAL dict data
            scalar axialDistance = elementData[j][0];
            scalar radius = elementData[j][1];
            scalar azimuthDegrees = elementData[j][2];
            scalar azimuthRadians = azimuthDegrees/180.0*mathematical::pi;
            scalar chordLength = elementData[j][3];
            scalar chordMount = elementData[j][4];
            
            // Compute frontal area contribution from this geometry segment
            if (j > 0)
            {
                scalar deltaAxial = axialDistance - elementData[j-1][0];
                scalar meanRadius = (radius + elementData[j-1][1])/2;
                frontalArea += mag(deltaAxial*meanRadius);
            }
            
            // Set sizes for actuatorLineSource elementGeometry lists
            elementGeometry[j].setSize(5);
            elementGeometry[j][0].setSize(3);
            elementGeometry[j][1].setSize(3);
            elementGeometry[j][2].setSize(1);
            elementGeometry[j][3].setSize(3);
            elementGeometry[j][4].setSize(1);
            
            // Create geometry point for AL source at origin
            vector point = origin_;
            // Move along axis
            point += axialDistance*axis_;
            scalar chordDisplacement = (0.5 - chordMount)*chordLength;
            point += chordDisplacement*freeStreamDirection_;
            point += radius*radialDirection_;
            initialVelocities[j] = -freeStreamDirection_*omega_*radius;
            // Rotate according to azimuth value
            rotateVector(point, origin_, axis_, azimuthRadians);
            rotateVector(initialVelocities[j], origin_, axis_, azimuthRadians);
            elementGeometry[j][0][0] = point.x(); // x location of geom point
            elementGeometry[j][0][1] = point.y(); // y location of geom point
            elementGeometry[j][0][2] = point.z(); // z location of geom point
            
            // Set span directions for AL source
            elementGeometry[j][1][0] = axis_.x(); // x component of span direction
            elementGeometry[j][1][1] = axis_.y(); // y component of span direction
            elementGeometry[j][1][2] = axis_.z(); // z component of span direction
            
            // Set chord length
            elementGeometry[j][2][0] = chordLength;
            
            // Set chord reference direction
            vector chordDirection = -freeStreamDirection_;
            rotateVector(chordDirection, origin_, axis_, azimuthRadians);
            elementGeometry[j][3][0] = chordDirection.x(); 
            elementGeometry[j][3][1] = chordDirection.y(); 
            elementGeometry[j][3][2] = chordDirection.z(); 
            
            // Set pitch
            scalar pitch = elementData[j][5];
            elementGeometry[j][4][0] = pitch;
        }
        
        // Add frontal area to list
        frontalAreas[i] = frontalArea;
        
        if (debug)
        {
            Info<< "Converted element geometry:" << endl << elementGeometry 
                << endl;
            Info<< "Frontal area from " << bladeName << ": " << frontalArea
                << endl;
        }
        
        bladeSubDict.add("elementGeometry", elementGeometry);
        bladeSubDict.add("initialVelocities", initialVelocities);
        
        dictionary dict;
        dict.add("actuatorLineSourceCoeffs", bladeSubDict);
        dict.add("type", "actuatorLineSource");
        dict.add("active", dict_.lookup("active"));
        dict.add("selectionMode", dict_.lookup("selectionMode"));
        dict.add("cellSet", dict_.lookup("cellSet"));
        
        actuatorLineSource* blade = new actuatorLineSource
        (
            bladeName, 
            modelType, 
            dict, 
            mesh_
        );
        
        blades_.set(i, blade);
    }
    
    // Frontal area is twice the maximum blade frontal area
    frontalArea_ = 2*max(frontalAreas);
    Info<< "Frontal area of " << name_ << ": " << frontalArea_ << endl;
}


void Foam::fv::crossFlowTurbineALSource::createStruts()
{
    int nStruts = strutsDict_.keys().size();
    struts_.setSize(nStruts);
    int nElements;
    word profileName;
    List<List<scalar> > elementData;
    List<List<scalar> > profileData;
    word modelType = "actuatorLineSource";
    List<word> strutNames = strutsDict_.toc();
    
    for (int i = 0; i < nStruts; i++)
    {
        word& strutName = strutNames[i];
        // Create dictionary items for this strut
        dictionary strutSubDict;
        strutSubDict = strutsDict_.subDict(strutName);
        strutSubDict.lookup("nElements") >> nElements;
        strutSubDict.lookup("profile") >> profileName;
        strutSubDict.lookup("elementData") >> elementData;
        profilesDict_.subDict(profileName).lookup("data") >> profileData;
        
        strutSubDict.add("freeStreamVelocity", freeStreamVelocity_);
        strutSubDict.add("fieldNames", coeffs_.lookup("fieldNames"));
        strutSubDict.add("coefficientData", profileData);
        strutSubDict.add("tipEffect", tipEffect_);
        
        if (debug)
        {
            Info<< "Creating actuator line strut " << strutName << endl;
            Info<< "Strut has " << nElements << " elements" << endl;
            Info<< "Strut profile: " << profileName << endl;
            Info<< "Element data:" << endl;
            Info<< elementData << endl << endl;
            Info<< "Profile sectional coefficient data:" << endl;
            Info<< profileData << endl << endl;
        }
        
        // Convert element data into actuator line element geometry
        label nGeomPoints = elementData.size();
        List<List<List<scalar> > > elementGeometry(nGeomPoints);
        List<vector> initialVelocities(nGeomPoints, vector::one);

        forAll(elementData, j)
        {
            // Read CFTAL dict data
            scalar axialDistance = elementData[j][0];
            scalar radius = elementData[j][1];
            scalar azimuthDegrees = elementData[j][2];
            scalar azimuthRadians = azimuthDegrees/180.0*mathematical::pi;
            scalar chordLength = elementData[j][3];
            scalar chordMount = elementData[j][4];
            
            // Set sizes for actuatorLineSource elementGeometry lists
            elementGeometry[j].setSize(5);
            elementGeometry[j][0].setSize(3);
            elementGeometry[j][1].setSize(3);
            elementGeometry[j][2].setSize(1);
            elementGeometry[j][3].setSize(3);
            elementGeometry[j][4].setSize(1);
            
            // Create geometry point for AL source at origin
            vector point = origin_;
            // Move along axis
            point += axialDistance*axis_;
            scalar chordDisplacement = (0.5 - chordMount)*chordLength;
            point += chordDisplacement*freeStreamDirection_;
            point += radius*radialDirection_;
            initialVelocities[j] = -freeStreamDirection_*omega_*radius;
            // Rotate according to azimuth value
            rotateVector(point, origin_, axis_, azimuthRadians);
            rotateVector(initialVelocities[j], origin_, axis_, azimuthRadians);
            elementGeometry[j][0][0] = point.x(); // x location of geom point
            elementGeometry[j][0][1] = point.y(); // y location of geom point
            elementGeometry[j][0][2] = point.z(); // z location of geom point
            
            // Set span directions for AL source (in radial direction)
            vector spanDirection = radialDirection_;
            rotateVector(spanDirection, origin_, axis_, azimuthRadians);
            elementGeometry[j][1][0] = spanDirection.x(); 
            elementGeometry[j][1][1] = spanDirection.y(); 
            elementGeometry[j][1][2] = spanDirection.z(); 
            
            // Set chord length
            elementGeometry[j][2][0] = chordLength;
            
            // Set chord reference direction
            vector chordDirection = -freeStreamDirection_;
            rotateVector(chordDirection, origin_, axis_, azimuthRadians);
            elementGeometry[j][3][0] = chordDirection.x(); 
            elementGeometry[j][3][1] = chordDirection.y(); 
            elementGeometry[j][3][2] = chordDirection.z(); 
            
            // Set pitch
            scalar pitch = elementData[j][5];
            elementGeometry[j][4][0] = pitch;
        }

        if (debug)
        {
            Info<< "Converted element geometry:" << endl << elementGeometry 
                << endl;
        }
        
        strutSubDict.add("elementGeometry", elementGeometry);
        strutSubDict.add("initialVelocities", initialVelocities);
        
        dictionary dict;
        dict.add("actuatorLineSourceCoeffs", strutSubDict);
        dict.add("type", "actuatorLineSource");
        dict.add("active", dict_.lookup("active"));
        dict.add("selectionMode", dict_.lookup("selectionMode"));
        dict.add("cellSet", dict_.lookup("cellSet"));
        
        actuatorLineSource* strut = new actuatorLineSource
        (
            strutName, 
            modelType, 
            dict, 
            mesh_
        );
        
        struts_.set(i, strut);
    }
}


void Foam::fv::crossFlowTurbineALSource::createShaft()
{
    int nElements;
    word profileName;
    List<List<scalar> > elementData;
    List<List<scalar> > profileData;
    dictionary shaftSubDict;
    
    shaftDict_.lookup("nElements") >> nElements;
    shaftDict_.lookup("profile") >> profileName;
    shaftDict_.lookup("elementData") >> elementData;
    profilesDict_.subDict(profileName).lookup("data") >> profileData;
    
    // Convert element data into actuator line element geometry
    label nGeomPoints = elementData.size();
    List<List<List<scalar> > > elementGeometry(nGeomPoints);
    List<vector> initialVelocities(nGeomPoints, vector::zero);

    forAll(elementData, j)
    {
        // Read shaft element data
        scalar axialDistance = elementData[j][0];
        scalar diameter = elementData[j][1];
        
        // Set sizes for actuatorLineSource elementGeometry lists
        elementGeometry[j].setSize(5);
        elementGeometry[j][0].setSize(3);
        elementGeometry[j][1].setSize(3);
        elementGeometry[j][2].setSize(1);
        elementGeometry[j][3].setSize(3);
        elementGeometry[j][4].setSize(1);
        
        // Create geometry point for AL source at origin
        vector point = origin_;
        // Move along axis
        point += axialDistance*axis_;
        
        elementGeometry[j][0][0] = point.x(); // x location of geom point
        elementGeometry[j][0][1] = point.y(); // y location of geom point
        elementGeometry[j][0][2] = point.z(); // z location of geom point
        
        // Set span directions
        elementGeometry[j][1][0] = axis_.x(); // x component of span direction
        elementGeometry[j][1][1] = axis_.y(); // y component of span direction
        elementGeometry[j][1][2] = axis_.z(); // z component of span direction
        
        // Set chord length
        elementGeometry[j][2][0] = diameter;
        
        // Set chord reference direction
        elementGeometry[j][3][0] = freeStreamDirection_.x();
        elementGeometry[j][3][1] = freeStreamDirection_.y();
        elementGeometry[j][3][2] = freeStreamDirection_.z();
        
        // Set pitch
        elementGeometry[j][4][0] = 0.0;
    }
    
    shaftSubDict.add("elementGeometry", elementGeometry);
    shaftSubDict.add("initialVelocities", initialVelocities);
    shaftSubDict.add("nElements", nElements);
    shaftSubDict.add("fieldNames", coeffs_.lookup("fieldNames"));
    shaftSubDict.add("coefficientData", profileData);
    shaftSubDict.add("tipEffect", tipEffect_);
    shaftSubDict.add("freeStreamVelocity", freeStreamVelocity_);
        
    dictionary dict;
    dict.add("actuatorLineSourceCoeffs", shaftSubDict);
    dict.add("type", "actuatorLineSource");
    dict.add("active", dict_.lookup("active"));
    dict.add("selectionMode", dict_.lookup("selectionMode"));
    dict.add("cellSet", dict_.lookup("cellSet"));
    
    actuatorLineSource* shaft = new actuatorLineSource
    (
        "shaft", 
        "actuatorLineSource", 
        dict, 
        mesh_
    );
    
    shaft_.set(shaft);
}


void Foam::fv::crossFlowTurbineALSource::createOutputFile()
{
    fileName dir;
    
    if (Pstream::parRun())
    {
        dir = time_.path()/"../postProcessing/turbines"
            / time_.timeName();
    }
    else
    {
        dir = time_.path()/"postProcessing/turbines"
            / time_.timeName();
    }
    
    if (!isDir(dir))
    {
        mkDir(dir);
    }

    outputFile_ = new OFstream(dir/name_ + ".csv");
    
    *outputFile_<< "time,angle_deg,tsr,cp,cd,ct" << endl;
}


void Foam::fv::crossFlowTurbineALSource::writePerf()
{
    *outputFile_<< time_.value() << "," << angleDeg_ << "," 
                << tipSpeedRatio_ << "," << powerCoefficient_ << "," 
                << dragCoefficient_ << "," << torqueCoefficient_ << endl;
}

void Foam::fv::crossFlowTurbineALSource::writeData(Ostream& os) const
{
    os  << indent << name_ << endl;
    dict_.write(os);
}

// * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * * //

Foam::fv::crossFlowTurbineALSource::crossFlowTurbineALSource
(
    const word& name,
    const word& modelType,
    const dictionary& dict,
    const fvMesh& mesh
)
:
    option(name, modelType, dict, mesh),
    time_(mesh.time()),
    rhoRef_(1.0),
    omega_(0.0),
    angleDeg_(0.0),
    nBlades_(0),
    hasStruts_(false),
    hasShaft_(false),
    freeStreamVelocity_(vector::zero),
    tipEffect_(1.0),
    forceField_
    (
        IOobject
        (
            "force." + name_,
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedVector
        (
            "force", 
            dimForce/dimVolume/dimDensity, 
            vector::zero
        )
    ),
    frontalArea_(0.0)
{
    read(dict);
    createCoordinateSystem();
    createBlades();
    if (hasStruts_) createStruts();
    if (hasShaft_) createShaft();
    lastRotationTime_ = time_.value();
    createOutputFile();
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::fv::crossFlowTurbineALSource::~crossFlowTurbineALSource()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

Foam::vector& Foam::fv::crossFlowTurbineALSource::force()
{
    return force_;
}


Foam::volVectorField& Foam::fv::crossFlowTurbineALSource::forceField()
{
    return forceField_;
}


Foam::scalar& Foam::fv::crossFlowTurbineALSource::torque()
{
    return torque_;
}


void Foam::fv::crossFlowTurbineALSource::rotate()
{
    scalar deltaT = time_.deltaT().value();
    scalar radians = omega_*deltaT;
    forAll(blades_, i)
    {
        blades_[i].rotate(origin_, axis_, radians);
    }
    
    if (hasStruts_)
    {
        forAll(struts_, i)
        {
            struts_[i].rotate(origin_, axis_, radians);
        }
    }
    
    if (hasShaft_)
    {
        shaft_->rotate(origin_, axis_, radians);
    }
    
    if (debug)
    {
        Info<< "Rotating " << name_ << " " << radians << " radians" 
            << endl << endl;
    }
    angleDeg_ += radians*180.0/Foam::constant::mathematical::pi;
    lastRotationTime_ = time_.value();
}


void Foam::fv::crossFlowTurbineALSource::addSup
(
    fvMatrix<vector>& eqn,
    const label fieldI
)
{
    bool write = false;
    // Rotate the turbine if time value has changed
    if (time_.value() != lastRotationTime_)
    {
        rotate();
        write = true;
    }

    // Zero out force vector and field
    forceField_ *= 0;
    force_ *= 0;
    
    // Create local moment vector
    vector moment(vector::zero);
    
    // Read the reference density for incompressible flow
    //coeffs_.lookup("rhoRef") >> rhoRef_;

    // Add source for blade actuator lines
    forAll(blades_, i)
    {
        blades_[i].addSup(eqn, fieldI);
        forceField_ += blades_[i].forceField();
        force_ += blades_[i].force();
        moment += blades_[i].moment(origin_);
    }
    
    if (hasStruts_)
    {
        // Add source for strut actuator lines
        forAll(struts_, i)
        {
            struts_[i].addSup(eqn, fieldI);
            forceField_ += struts_[i].forceField();
            force_ += struts_[i].force();
            moment += struts_[i].moment(origin_);
        }
    }
    
    if (hasShaft_)
    {
        // Add source for shaft actuator line
        shaft_->addSup(eqn, fieldI);
        forceField_ += shaft_->forceField();
        force_ += shaft_->force();
        moment += shaft_->moment(origin_);
    }
    
    // Torque is the projection of the moment from all blades on the axis
    torque_ = moment & axis_;
    Info<< "Azimuthal angle (degrees) of " << name_ << ": " << angleDeg_ 
        << endl;
    Info<< "Torque (per unit density) from " << name_ << ": " << torque_ 
        << endl;
        
    torqueCoefficient_ = torque_/(0.5*frontalArea_*rotorRadius_
                       * magSqr(freeStreamVelocity_));
    powerCoefficient_ = torqueCoefficient_*tipSpeedRatio_;
    dragCoefficient_ = force_ & freeStreamDirection_
                     / (0.5*frontalArea_*magSqr(freeStreamVelocity_));
                             
    Info<< "Power coefficient from " << name_ << ": " << powerCoefficient_
        << endl << endl;
        
    // Write performance data if time value has changed and master processor
    if (write and Pstream::master())
    {
        writePerf();
    }
}


void Foam::fv::crossFlowTurbineALSource::addSup
(
    const volScalarField& rho,
    fvMatrix<vector>& eqn,
    const label fieldI
)
{
    // Rotate the turbine if time value has changed
    if (time_.value() != lastRotationTime_)
    {
        rotate();
    }

    // Zero out force vector and field
    forceField_ *= 0;
    force_ *= 0;
    
    // Create local moment vector
    vector moment(vector::zero);

    // Add source for all actuator lines
    forAll(blades_, i)
    {
        blades_[i].addSup(rho, eqn, fieldI);
        forceField_ += blades_[i].forceField();
        force_ += blades_[i].force();
    }
    
    // Torque is the projection of the moment from all blades on the axis
    torque_ = moment & axis_;
    Info<< "Azimuthal angle (degrees) of " << name_ << ": " << angleDeg_ 
        << endl;
    Info<< "Torque (per unit density) from " << name_ << ": " << torque_ 
        << endl << endl;
}


void Foam::fv::crossFlowTurbineALSource::printCoeffs() const
{
    Info<< "Number of blades: " << nBlades_ << endl;
}


bool Foam::fv::crossFlowTurbineALSource::read(const dictionary& dict)
{
    if (option::read(dict))
    {
        
        coeffs_.lookup("fieldNames") >> fieldNames_;
        applied_.setSize(fieldNames_.size(), false);

        // Read coordinate system/geometry invariant properties
        coeffs_.lookup("origin") >> origin_;
        coeffs_.lookup("axis") >> axis_;
        coeffs_.lookup("freeStreamVelocity") >> freeStreamVelocity_;
        coeffs_.lookup("tipSpeedRatio") >> tipSpeedRatio_;
        coeffs_.lookup("rotorRadius") >> rotorRadius_;
        omega_ = tipSpeedRatio_*mag(freeStreamVelocity_)/rotorRadius_;

        // Get blade information
        bladesDict_ = coeffs_.subDict("blades");
        nBlades_ = bladesDict_.keys().size();
        bladeNames_ = bladesDict_.toc();

        coeffs_.lookup("tipEffect") >> tipEffect_;
        
        // Get struts information
        strutsDict_ = coeffs_.subOrEmptyDict("struts");
        if (strutsDict_.keys().size() > 0) hasStruts_ = true;
        
        // Get shaft information
        shaftDict_ = coeffs_.subOrEmptyDict("shaft");
        if (shaftDict_.keys().size() > 0) hasShaft_ = true;
        
        // Get profiles information
        profilesDict_ = coeffs_.subDict("profiles");
        
        if (debug)
        {
            Info<< "Debugging on" << endl;
            Info<< "Cross-flow turbine properties:" << endl;
            printCoeffs();
        }

        return true;
    }
    else
    {
        return false;
    }
}


// ************************************************************************* //
