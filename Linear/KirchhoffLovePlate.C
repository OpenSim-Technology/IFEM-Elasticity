// $Id$
//==============================================================================
//!
//! \file KirchhoffLovePlate.C
//!
//! \date Sep 13 2011
//!
//! \author Knut Morten Okstad / SINTEF
//!
//! \brief Class for linear Kirchhoff-Love thin plate problems.
//!
//==============================================================================

#include "KirchhoffLovePlate.h"
#include "LinIsotropic.h"
#include "FiniteElement.h"
#include "Utilities.h"
#include "ElmMats.h"
#include "ElmNorm.h"
#include "Tensor.h"
#include "Vec3Oper.h"
#include "AnaSol.h"
#include "VTF.h"
#include "IFEM.h"


KirchhoffLovePlate::KirchhoffLovePlate (unsigned short int n) : nsd(n)
{
  npv = 1; // Number of primary unknowns per node

  gravity = 0.0;
  thickness = 0.1;

  material = NULL;
  locSys = NULL;
  presFld = NULL;
  eM = eK = 0;
  eS = 0;
}


KirchhoffLovePlate::~KirchhoffLovePlate ()
{
  if (locSys) delete locSys;
}


void KirchhoffLovePlate::printLog () const
{
  IFEM::cout <<"KirchhoffLovePlate: thickness = "<< thickness
             <<", gravity = "<< gravity << std::endl;

  if (!material)
  {
    static LinIsotropic defaultMat;
    const_cast<KirchhoffLovePlate*>(this)->material = &defaultMat;
  }

  material->printLog();
}


void KirchhoffLovePlate::setMode (SIM::SolutionMode mode)
{
  m_mode = mode;
  eM = eK = 0;
  eS = 0;

  if (mode == SIM::RECOVERY)
    primsol.resize(1);
  else
    primsol.clear();

  switch (mode)
    {
    case SIM::STATIC:
      eK = 1;
      eS = 1;
      break;

    case SIM::VIBRATION:
      eK = 1;
      eM = 2;
      break;

    case SIM::STIFF_ONLY:
      eK = 1;
      break;

    case SIM::RHS_ONLY:
      eS = 1;
      break;

    default:
      ;
    }
}


LocalIntegral* KirchhoffLovePlate::getLocalIntegral (size_t nen, size_t,
						     bool neumann) const
{
  ElmMats* result = new ElmMats();
  switch (m_mode)
    {
    case SIM::STATIC:
      result->rhsOnly = neumann;
      result->withLHS = !neumann;
      result->resize(neumann ? 0 : 1, 1);
      break;

    case SIM::VIBRATION:
      result->resize(2,0);
      break;

    case SIM::STIFF_ONLY:
      result->resize(1,0);
      break;

    case SIM::RHS_ONLY:
      result->resize(neumann ? 0 : 1, 1);

    case SIM::RECOVERY:
      result->rhsOnly = true;
      result->withLHS = false;
      break;

    default:
      ;
    }

  result->redim(nen);
  return result;
}


double KirchhoffLovePlate::getPressure (const Vec3& X) const
{
  double p = material->getMassDensity(X)*gravity*thickness;

  if (presFld)
    p += (*presFld)(X);

  return p;
}


bool KirchhoffLovePlate::haveLoads () const
{
  if (presFld) return true;

  if (gravity != 0.0 && material)
    return material->getMassDensity(Vec3()) != 0.0;

  return false;
}


void KirchhoffLovePlate::initIntegration (size_t nGp, size_t)
{
  presVal.resize(nGp,std::make_pair(Vec3(),Vec3()));
}


bool KirchhoffLovePlate::writeGlvT (VTF* vtf, int iStep,
                                    int& geoBlk, int& nBlock) const
{
  if (presVal.empty())
    return true;
  else if (!vtf)
    return false;

  // Write surface pressures as discrete point vectors to the VTF-file
  return vtf->writeVectors(presVal,geoBlk,++nBlock,"Pressure",iStep);
}


/*!
  The strain-displacement matrix for a Kirchhoff-Love plate element is formally
  defined as:
  \f[
  [B] = \left[\begin{array}{c}
   \frac{\partial^2}{\partial x^2} \\
   \frac{\partial^2}{\partial y^2} \\
  2\frac{\partial^2}{\partial x\partial y}
  \end{array}\right] [N]
  \f]
  where
  [\a N ] is the element basis functions arranged in a [NENOD] row-vector.
*/

bool KirchhoffLovePlate::formBmatrix (Matrix& Bmat,
				      const Matrix3D& d2NdX2) const
{
  const size_t nenod = d2NdX2.dim(1);
  const size_t nstrc = nsd*(nsd+1)/2;

  Bmat.resize(nstrc,nenod,true);

  if (d2NdX2.dim(2) != nsd || d2NdX2.dim(3) != nsd)
  {
    std::cerr <<" *** KirchhoffLovePlate::formBmatrix: Invalid dimension on"
	      <<" d2NdX2, "<< d2NdX2.dim(1) <<"x"<< d2NdX2.dim(2)
	      <<"x"<< d2NdX2.dim(3) <<"."<< std::endl;
    return false;
  }

  for (size_t i = 1; i <= nenod; i++)
    if (nsd == 1)
      Bmat(1,i) = d2NdX2(i,1,1);
    else
    {
      Bmat(1,i) = d2NdX2(i,1,1);
      Bmat(2,i) = d2NdX2(i,2,2);
      Bmat(3,i) = d2NdX2(i,1,2)*2.0;
    }

  return true;
}


bool KirchhoffLovePlate::formCmatrix (Matrix& C, const Vec3& X,
				      bool invers) const
{
  SymmTensor dummy(nsd); double U;
  if (!material->evaluate(C,dummy,U,0,X,dummy,dummy, invers ? -1 : 1))
    return false;

  double factor = thickness*thickness*thickness/12.0;
  C.multiply(invers ? 1.0/factor : factor);
  return true;
}


void KirchhoffLovePlate::formMassMatrix (Matrix& EM, const Vector& N,
					 const Vec3& X, double detJW) const
{
  double rho = material->getMassDensity(X)*thickness;

  if (rho != 0.0)
    EM.outer_product(N,N*rho*detJW,true);
}


void KirchhoffLovePlate::formBodyForce (Vector& ES, const Vector& N, size_t iP,
					const Vec3& X, double detJW) const
{
  double p = this->getPressure(X);
  if (p != 0.0)
  {
    ES.add(N,p*detJW);
    // Store pressure value for visualization
    if (iP < presVal.size())
      presVal[iP] = std::make_pair(X,Vec3(0.0,0.0,p));
  }
}


bool KirchhoffLovePlate::evalInt (LocalIntegral& elmInt,
				  const FiniteElement& fe,
				  const Vec3& X) const
{
  ElmMats& elMat = static_cast<ElmMats&>(elmInt);

  if (eK)
  {
    // Compute the strain-displacement matrix B from d2NdX2
    Matrix Bmat;
    if (!this->formBmatrix(Bmat,fe.d2NdX2)) return false;

    // Evaluate the constitutive matrix at this point
    Matrix Cmat;
    if (!this->formCmatrix(Cmat,X)) return false;

    // Integrate the stiffness matrix
    Matrix CB;
    CB.multiply(Cmat,Bmat).multiply(fe.detJxW); // CB = C*B*|J|*w
    elMat.A[eK-1].multiply(Bmat,CB,true,false,true); // EK += B^T * CB
  }

  if (eM)
    // Integrate the mass matrix
    this->formMassMatrix(elMat.A[eM-1],fe.N,X,fe.detJxW);

  if (eS)
    // Integrate the load vector due to gravitation and other body forces
    this->formBodyForce(elMat.b[eS-1],fe.N,fe.iGP,X,fe.detJxW);

  return true;
}


bool KirchhoffLovePlate::evalBou (LocalIntegral& elmInt,
				  const FiniteElement& fe,
				  const Vec3& X, const Vec3& normal) const
{
  std::cerr <<" *** KirchhoffLovePlate::evalBou not implemented."<< std::endl;
  return false;
}


bool KirchhoffLovePlate::evalSol (Vector& s, const FiniteElement& fe,
				  const Vec3& X,
				  const std::vector<int>& MNPC) const
{
  // Extract element displacements
  int ierr = 0;
  Vector eV;
  if (!primsol.empty() && !primsol.front().empty())
    if ((ierr = utl::gather(MNPC,1,primsol.front(),eV)))
    {
      std::cerr <<" *** KirchhoffLovePlate::evalSol: Detected "
		<< ierr <<" node numbers out of range."<< std::endl;
      return false;
    }

  // Evaluate the stress resultant tensor
  return this->evalSol(s,eV,fe.d2NdX2,X,true);
}


bool KirchhoffLovePlate::evalSol (Vector& s, const Vector& eV,
				  const Matrix3D& d2NdX2, const Vec3& X,
				  bool toLocal) const
{
  if (eV.empty())
  {
    std::cerr <<" *** KirchhoffLovePlate::evalSol: No displacement vector."
	      << std::endl;
    return false;
  }
  else if (eV.size() != d2NdX2.dim(1))
  {
    std::cerr <<" *** KirchhoffLovePlate::evalSol: Invalid displacement vector."
	      <<"\n     size(eV) = "<< eV.size() <<"   size(d2NdX2) = "
	      << d2NdX2.dim(1) <<","<< d2NdX2.dim(2)*d2NdX2.dim(3) << std::endl;
    return false;
  }

  // Compute the strain-displacement matrix B from d2NdX2
  Matrix Bmat;
  if (!this->formBmatrix(Bmat,d2NdX2))
    return false;

  // Evaluate the constitutive matrix at this point
  Matrix Cmat;
  if (!this->formCmatrix(Cmat,X))
    return false;

  // Evaluate the curvature tensor
  SymmTensor kappa(nsd), m(nsd);
  if (!Bmat.multiply(eV,kappa)) // kappa = B*eV
    return false;

  // Evaluate the stress resultant tensor
  if (!Cmat.multiply(-1.0*kappa,m)) // m = -C*kappa
    return false;

  // Congruence transformation to local coordinate system at current point
  if (toLocal && locSys) m.transform(locSys->getTmat(X));

  s = m;
  return true;
}


size_t KirchhoffLovePlate::getNoFields (int fld) const
{
  return fld < 2 ? 1 : nsd*(nsd+1)/2;
}


std::string KirchhoffLovePlate::getField1Name (size_t,
					       const char* prefix) const
{
  if (!prefix) return "w";

  return prefix + std::string(" w");
}


std::string KirchhoffLovePlate::getField2Name (size_t i,
					       const char* prefix) const
{
  if (i >= 3) return NULL;

  static const char* s[6] = { "m_xx", "m_yy", "m_xy" };

  std::string name;
  if (prefix)
    name = std::string(prefix) + " ";
  else
    name.clear();

  name += s[i];

  return name;
}


NormBase* KirchhoffLovePlate::getNormIntegrand (AnaSol* asol) const
{
  if (asol)
    return new KirchhoffLovePlateNorm(*const_cast<KirchhoffLovePlate*>(this),
				      asol->getStressSol());
  else
    return new KirchhoffLovePlateNorm(*const_cast<KirchhoffLovePlate*>(this));
}


KirchhoffLovePlateNorm::KirchhoffLovePlateNorm (KirchhoffLovePlate& p,
						STensorFunc* a)
  : NormBase(p), anasol(a)
{
  nrcmp = myProblem.getNoFields(2);
}


bool KirchhoffLovePlateNorm::evalInt (LocalIntegral& elmInt,
				      const FiniteElement& fe,
				      const Vec3& X) const
{
  KirchhoffLovePlate& problem = static_cast<KirchhoffLovePlate&>(myProblem);
  ElmNorm& pnorm = static_cast<ElmNorm&>(elmInt);

  // Evaluate the inverse constitutive matrix at this point
  Matrix Cinv;
  if (!problem.formCmatrix(Cinv,X,true))
    return false;

  // Evaluate the finite element stress field
  Vector mh, m, error;
  if (!problem.evalSol(mh,pnorm.vec.front(),fe.d2NdX2,X))
    return false;

  size_t ip = 0;
  // Integrate the energy norm a(w^h,w^h)
  pnorm[ip++] += mh.dot(Cinv*mh)*fe.detJxW;

  if (problem.haveLoads())
  {
    // Evaluate the body load
    double p = problem.getPressure(X);
    // Evaluate the displacement field
    double w = pnorm.vec.front().dot(fe.N);
    // Integrate the external energy (p,w^h)
    pnorm[ip] += p*w*fe.detJxW;
  }
  ip++;

  if (anasol)
  {
    // Evaluate the analytical stress resultant field
    m = (*anasol)(X);

    // Integrate the energy norm a(w,w)
    pnorm[ip++] += m.dot(Cinv*m)*fe.detJxW;
    // Integrate the error in energy norm a(w-w^h,w-w^h)
    error = m - mh;
    pnorm[ip++] += error.dot(Cinv*error)*fe.detJxW;
  }

  size_t i, j;
  for (i = 0; i < pnorm.psol.size(); i++)
    if (!pnorm.psol[i].empty())
    {
      // Evaluate projected stress resultant field
      Vector mr(mh.size());
      for (j = 0; j < nrcmp; j++)
	mr[j] = pnorm.psol[i].dot(fe.N,j,nrcmp);

      // Integrate the energy norm a(w^r,w^r)
      pnorm[ip++] += mr.dot(Cinv*mr)*fe.detJxW;
      // Integrate the error in energy norm a(w^r-w^h,w^r-w^h)
      error = mr - mh;
      pnorm[ip++] += error.dot(Cinv*error)*fe.detJxW;

      // Integrate the L2-norm (m^r,m^r)
      pnorm[ip++] += mr.dot(mr)*fe.detJxW;
      // Integrate the error in L2-norm (m^r-m^h,m^r-m^h)
      pnorm[ip++] += error.dot(error)*fe.detJxW;

      if (anasol)
      {
        // Integrate the error in the projected solution a(w-w^r,w-w^r)
        error = m - mr;
        pnorm[ip++] += error.dot(Cinv*error)*fe.detJxW;
        ip++; // Make room for the local effectivity index here
      }
    }

  return true;
}


bool KirchhoffLovePlateNorm::evalBou (LocalIntegral& elmInt,
				      const FiniteElement& fe,
				      const Vec3& X, const Vec3& normal) const
{
  std::cerr <<" *** KirchhoffLovePlateNorm::evalBou not included."<< std::endl;
  return false;
}


bool KirchhoffLovePlateNorm::finalizeElement (LocalIntegral& elmInt)
{
  if (!anasol) return true;

  ElmNorm& pnorm = static_cast<ElmNorm&>(elmInt);

  // Evaluate local effectivity indices as sqrt(a(e^r,e^r)/a(e,e))
  // with e^r = w^r - w^h  and  e = w - w^h
  for (size_t ip = 9; ip < pnorm.size(); ip += 6)
    pnorm[ip] = sqrt(pnorm[ip-4] / pnorm[3]);

  return true;
}


void KirchhoffLovePlateNorm::addBoundaryTerms (Vectors& gNorm,
					       double energy) const
{
  gNorm.front()(2) += energy;
}


size_t KirchhoffLovePlateNorm::getNoFields (int group) const
{
  if (group < 1)
    return this->NormBase::getNoFields();
  else if (group == 1)
    return anasol ? 4 : 2;
  else
    return anasol ? 6 : 4;
}


std::string KirchhoffLovePlateNorm::getName (size_t i, size_t j,
                                             const char* prefix) const
{
  if (i == 0 || j == 0 || j > 6 || (i == 1 && j > 4))
    return this->NormBase::getName(i,j,prefix);

  static const char* u[4] = {
    "a(w^h,w^h)^0.5",
    "(p,w^h)^0.5",
    "a(w,w)^0.5",
    "a(e,e)^0.5, e=w-w^h"
  };

  static const char* p[6] = {
    "a(w^r,w^r)^0.5",
    "a(e,e)^0.5, e=w^r-w^h",
    "(w^r,w^r)^0.5",
    "(e,e)^0.5, e=w^r-w^h",
    "a(e,e)^0.5, e=w-w^r",
    "effectivity index"
  };

  const char** s = i > 1 ? p : u;

  if (!prefix)
    return s[j-1];

  return prefix + std::string(" ") + s[j-1];
}
