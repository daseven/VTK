/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkThinPlateSplineTransform.cxx
  Language:  C++
  Date:      $Date$
  Version:   $Revision$
  Thanks:    Thanks to David G. Gobbi who developed this class 
             based on code from vtkThinPlateSplineMeshWarp.cxx
	     written by Tim Hutton.

Copyright (c) 1993-2001 Ken Martin, Will Schroeder, Bill Lorensen 
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

 * Neither name of Ken Martin, Will Schroeder, or Bill Lorensen nor the names
   of any contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

 * Modified source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

=========================================================================*/
#include "vtkThinPlateSplineTransform.h"
#include "vtkMath.h"
#include "vtkObjectFactory.h"

//----------------------------------------------------------------------------
vtkThinPlateSplineTransform* vtkThinPlateSplineTransform::New()
{
  // First try to create the object from the vtkObjectFactory
  vtkObject* ret = vtkObjectFactory::CreateInstance("vtkThinPlateSplineTransform");
  if(ret)
    {
    return (vtkThinPlateSplineTransform*)ret;
    }
  // If the factory was unable to create the object, then create it here.
  return new vtkThinPlateSplineTransform;
}

//------------------------------------------------------------------------
// some dull matrix things

static inline double** NewMatrix(int rows, int cols) 
{
  double *matrix = new double[rows*cols];
  double **m = new double *[rows];
  for(int i = 0; i < rows; i++) 
    {
    m[i] = &matrix[i*cols];
    }
  return m;
}

//------------------------------------------------------------------------
static inline void DeleteMatrix(double **m) 
{
  delete [] *m;
  delete [] m;
}

//------------------------------------------------------------------------
static inline void ZeroMatrix(double **m, int rows, int cols) 
{
  for(int i = 0; i < rows; i++) 
    {
    for(int j = 0; j < cols; j++) 
      {
      m[i][j] = 0.0;
      }
    }
}

//------------------------------------------------------------------------
static inline void MatrixMultiply(double **a, double **b, double **c,
				  int arows, int acols, 
				  int brows, int bcols) 
{
  if(acols != brows) 
    {
    return;	// acols must equal br otherwise we can't proceed
    }
	
  // c must have size arows*bcols (we assume this)

  for(int i = 0; i < arows; i++) 
    {
    for(int j = 0; j < bcols; j++) 
      {
      c[i][j] = 0.0;
      for(int k = 0; k < acols; k++)
        {
        c[i][j] += a[i][k]*b[k][j];
        }
      }
    }
}

//------------------------------------------------------------------------
static inline void MatrixTranspose(double **a, double **b, int rows, int cols)
{
  for(int i = 0; i < rows; i++) 
    {
    for(int j = 0; j < cols; j++) 
      {
      double tmp = a[i][j];
      b[i][j] = a[j][i];
      b[j][i] = tmp;
      }
    }
}

//------------------------------------------------------------------------
vtkThinPlateSplineTransform::vtkThinPlateSplineTransform()
{
  this->SourceLandmarks=NULL;
  this->TargetLandmarks=NULL;
  this->Sigma = 1.0;

  // If the InverseFlag is set, then we use an iterative
  // method to invert the transformation.  
  // The InverseTolerance sets the precision to which we want to 
  // calculate the inverse.
  this->InverseTolerance = 0.001;
  this->InverseIterations = 500;

  this->Basis = -1;
  this->SetBasisToR2LogR();

  this->NumberOfPoints = 0;
  this->MatrixW = NULL;
}

//------------------------------------------------------------------------
vtkThinPlateSplineTransform::~vtkThinPlateSplineTransform()
{
  if (this->SourceLandmarks)
    {
    this->SourceLandmarks->Delete();
    }
  if (this->TargetLandmarks)
    {
    this->TargetLandmarks->Delete();
    }
  if (this->MatrixW)
    {
    DeleteMatrix(this->MatrixW);
    this->MatrixW = NULL;
    }
}

//------------------------------------------------------------------------
void vtkThinPlateSplineTransform::SetSourceLandmarks(vtkPoints *source)
{
  if (this->SourceLandmarks == source)
    {
    return;
    }

  if (this->SourceLandmarks)
    {
    this->SourceLandmarks->Delete();
    }

  source->Register(this);
  this->SourceLandmarks = source;

  this->Modified();
}

//------------------------------------------------------------------------
void vtkThinPlateSplineTransform::SetTargetLandmarks(vtkPoints *target)
{
  if (this->TargetLandmarks == target)
    {
    return;
    }

  if (this->TargetLandmarks)
    {
    this->TargetLandmarks->Delete();
    }

  target->Register(this);
  this->TargetLandmarks = target;
  this->Modified();
}

//------------------------------------------------------------------------
unsigned long vtkThinPlateSplineTransform::GetMTime()
{
  unsigned long result = this->vtkWarpTransform::GetMTime();
  unsigned long mtime;

  if (this->SourceLandmarks)
    {
    mtime = this->SourceLandmarks->GetMTime(); 
    if (mtime > result)
      {
      result = mtime;
      }
    }
  if (this->TargetLandmarks)
    {
    mtime = this->TargetLandmarks->GetMTime(); 
    if (mtime > result)
      {
      result = mtime;
      }
    }
  return result;
}

//------------------------------------------------------------------------
void vtkThinPlateSplineTransform::InternalUpdate()
{
  if (this->SourceLandmarks == NULL || this->TargetLandmarks == NULL)
    {
    if (this->MatrixW)
      {
      DeleteMatrix(this->MatrixW);
      }
    this->MatrixW = NULL;
    this->NumberOfPoints = 0;
    return;
    }

  if (this->SourceLandmarks->GetNumberOfPoints() !=
      this->TargetLandmarks->GetNumberOfPoints())
    {
    vtkErrorMacro("Update: Source and Target Landmarks contain a different number of points");
    return;
    }

  const int N = this->SourceLandmarks->GetNumberOfPoints();
  const int D = 3; // dimensions

  // the output weights matrix
  double **W = NewMatrix(N+D+1,D); 
  double **A = &W[N+1];  // the linear rotation + scale matrix 
  double *C = W[N];      // the linear translation 

  if (N >= 3)
    {
    // Notation and inspiration from:
    // Fred L. Bookstein (1997) "Shape and the Information in Medical Images: 
    // A Decade of the Morphometric Synthesis" Computer Vision and Image 
    // Understanding 66(2):97-118
    // and online work published by Tim Cootes (http://www.wiau.man.ac.uk/~bim)
	
    // the input matrices
    double **L = NewMatrix(N+D+1,N+D+1);
    double **X = NewMatrix(N+D+1,D);

    // build L
    // will leave the bottom-right corner with zeros
    ZeroMatrix(L,N+D+1,N+D+1);

    int q,c;
    double p[3],p2[3];
    double dx,dy,dz;
    double r;
    double (*phi)(double) = this->BasisFunction;
    
    for(q = 0; q < N; q++)
      {
      this->SourceLandmarks->GetPoint(q,p);
      // fill in the top-right and bottom-left corners of L (Q)
      L[N][q] = L[q][N] = 1.0;
      L[N+1][q] = L[q][N+1] = p[0];
      L[N+2][q] = L[q][N+2] = p[1];
      L[N+3][q] = L[q][N+3] = p[2];
      // fill in the top-left corner of L (K), using symmetry
      for(c = 0; c < q; c++)
	{
        this->SourceLandmarks->GetPoint(c,p2);
	dx = p[0]-p2[0]; dy = p[1]-p2[1]; dz = p[2]-p2[2];
	r = sqrt(dx*dx + dy*dy + dz*dz);
	L[q][c] = L[c][q] = phi(r/this->Sigma);
	}
      }
    
    // build X
    ZeroMatrix(X,N+D+1,D);
    for (q = 0; q < N; q++)
      {
      this->TargetLandmarks->GetPoint(q,p);
      X[q][0] = p[0];
      X[q][1] = p[1];
      X[q][2] = p[2];
      }
    
    // solve for W, where W = Inverse(L)*X; 

    // this is done via eigenvector decomposition so
    // that we can avoid singular values
    // W = V*Inverse(w)*U*X  
    
    double *values = new double[N+D+1];
    double **V = NewMatrix(N+D+1,N+D+1);
    double **w = NewMatrix(N+D+1,N+D+1);
    double **U = L;  // reuse the space
    vtkMath::JacobiN(L,N+D+1,values,V);
    MatrixTranspose(V,U,N+D+1,N+D+1);
    
    int i,j;
    double maxValue = 0.0; // maximum eigenvalue
    for (i = 0; i < N+D+1; i++)
      {
      double tmp = fabs(values[i]);
      if (tmp > maxValue)
	{
	maxValue = tmp;
	}
      } 

    for (i = 0; i < N+D+1; i++)
      {
      for (j = 0; j < N+D+1; j++)
	{
        w[i][j] = 0.0;
	}
      // here's the trick: don't invert the singular values
      if (fabs(values[i]/maxValue) > 1e-16)
	{
        w[i][i] = 1.0/values[i];
	}
      }
    delete [] values;
    
    MatrixMultiply(U,X,W,N+D+1,N+D+1,N+D+1,D);
    MatrixMultiply(w,W,X,N+D+1,N+D+1,N+D+1,D);
    MatrixMultiply(V,X,W,N+D+1,N+D+1,N+D+1,D);
    
    DeleteMatrix(V);
    DeleteMatrix(w);
    DeleteMatrix(U);
    DeleteMatrix(X);

    // now the linear portion of the warp must be checked
    // (this is a very poor check for now)
    if (fabs(vtkMath::Determinant3x3((double (*)[3]) *A)) < 1e-16)
      {
      for (i = 0; i < 3; i++)
	{
	if (sqrt(A[0][i]*A[0][i] + A[1][i]*A[1][i] + A[2][i]*A[2][i])
	    < 1e-16)
	  {
	  A[0][i] = A[1][i] = A[2][i] = A[i][0] = A[i][1] = A[i][2] = 0;
	  A[i][i] = 1.0;
	  }
	}
      }
    }
  // special cases, I added these to ensure that this class doesn't 
  // misbehave if the user supplied fewer than 3 landmarks
  else // (N < 3)
    {
    int i,j;
    // set nonlinear portion of transformation to zero
    for (i = 0; i < N; i++)
      {
      for (j = 0; j < D; j++)
	{
	W[i][j] = 0;
	}
      }

    if (N == 2)
      { // two landmarks, construct a similarity transformation
      double s0[3],t0[3],s1[3],t1[3];
      this->SourceLandmarks->GetPoint(0,s0);
      this->TargetLandmarks->GetPoint(0,t0);
      this->SourceLandmarks->GetPoint(1,s1);
      this->TargetLandmarks->GetPoint(1,t1);

      double ds[3],dt[3],as[3],at[3];
      double rs = 0, rt = 0;
      for (i = 0; i < 3; i++)
	{
	as[i] = (s0[i] + s1[i])/2;  // average of endpoints
	ds[i] = s1[i] - s0[i];      // vector between points
	rs += ds[i]*ds[i];
	at[i] = (t0[i] + t1[i])/2;
	dt[i] = t1[i] - t0[i];
	rt += dt[i]*dt[i];
	}

      // normalize the two vectors
      rs = sqrt(rs);
      ds[0] /= rs; ds[1] /= rs; ds[2] /= rs; 
      rt = sqrt(rt);
      dt[0] /= rt; dt[1] /= rt; dt[2] /= rt; 

      double w,x,y,z;
      // take dot & cross product
      w = ds[0]*dt[0] + ds[1]*dt[1] + ds[2]*dt[2];
      x = ds[1]*dt[2] - ds[2]*dt[1];
      y = ds[2]*dt[0] - ds[0]*dt[2];
      z = ds[0]*dt[1] - ds[1]*dt[0];

      double r = sqrt(x*x + y*y + z*z);
      double theta = atan2(r,w);

      // construct quaternion
      w = cos(theta/2);
      if (r != 0)
	{
	r = sin(theta/2)/r;
	x = x*r;
	y = y*r;
	z = z*r;
	}
      else // rotation by 180 degrees
	{
	// rotate around a vector perpendicular to ds
	vtkMath::Perpendiculars(ds,dt,0,0);
	r = sin(theta/2);
	x = dt[0]*r;
	y = dt[1]*r;
	z = dt[2]*r;
	}
      
      // now r is scale factor for matrix
      r = rt/rs;

      // build a rotation + scale matrix
      A[0][0] = (w*w + x*x - y*y - z*z)*r;
      A[0][1] = (x*y + w*z)*2*r;
      A[0][2] = (x*z - w*y)*2*r;

      A[1][0] = (x*y - w*z)*2*r;
      A[1][1] = (w*w - x*x + y*y - z*z)*r;
      A[1][2] = (y*z + w*x)*2*r;

      A[2][0] = (x*z + w*y)*2*r;
      A[2][1] = (y*z - w*x)*2*r;
      A[2][2] = (w*w - x*x - y*y + z*z)*r;

      // include the translation
      C[0] = at[0] - as[0]*A[0][0] - as[1]*A[1][0] - as[2]*A[2][0];
      C[1] = at[1] - as[0]*A[0][1] - as[1]*A[1][1] - as[2]*A[2][1];
      C[2] = at[2] - as[0]*A[0][2] - as[1]*A[1][2] - as[2]*A[2][2];
      }
    else if (N == 1) // one landmark, translation only
      {
      double p[3],p2[3];
      this->SourceLandmarks->GetPoint(0,p);
      this->TargetLandmarks->GetPoint(0,p2);
      
      for (i = 0; i < D; i++)
	{
        for (j = 0; j < D; j++)
	  {
	  A[i][j] = 0;
	  }
	A[i][i] = 1;
	C[i] = p2[i] - p[i];
	}
      }

    else // if no landmarks, set to identity
      {
      for (i = 0; i < D; i++)
	{
        for (j = 0; j < D; j++)
	  {
	  A[i][j] = 0;
	  }
	A[i][i] = 1;
	C[i] = 0;
	}
      }
    }

  // left in for debug purposes
  /*
  cerr << "W =\n";
  for (int i = 0; i < N+1+D; i++)
    {
    cerr << W[i][0] << ' ' << W[i][1] << ' ' << W[i][2] << '\n';
    }
  cerr << "\n";
  */

  if (this->MatrixW)
    {
    DeleteMatrix(this->MatrixW);
    }
  this->MatrixW = W;
  this->NumberOfPoints = N;
}

//------------------------------------------------------------------------
// The matrix W was created by Update.  Not much has to be done to
// apply the transform:  do an affine transformation, then do
// perturbations based on the landmarks.
template<class T>
static inline void vtkThinPlateSplineForwardTransformPoint(
					   vtkThinPlateSplineTransform *self,
					   double **W, int N,
					   double (*phi)(double),
					   const T point[3], T output[3])
{
  if (N == 0)
    {
    output[0] = point[0];
    output[1] = point[1];
    output[2] = point[2];
    return;
    }

  double *C = W[N]; 
  double **A = &W[N+1];

  double dx,dy,dz;
  double p[3];
  double U,r;
  double invSigma = 1.0/self->GetSigma();

  double x = 0, y = 0, z = 0; 

  vtkPoints *sourceLandmarks = self->GetSourceLandmarks();

  // do the nonlinear stuff
  for(int i = 0; i < N; i++)
    {
    sourceLandmarks->GetPoint(i,p);
    dx = point[0]-p[0]; dy = point[1]-p[1]; dz = point[2]-p[2];
    r = sqrt(dx*dx + dy*dy + dz*dz);
    U = phi(r*invSigma);
    x += U*W[i][0];
    y += U*W[i][1];
    z += U*W[i][2];
    }

  // finish off with the affine transformation
  x += C[0] + point[0]*A[0][0] + point[1]*A[1][0] + point[2]*A[2][0];
  y += C[1] + point[0]*A[0][1] + point[1]*A[1][1] + point[2]*A[2][1];
  z += C[2] + point[0]*A[0][2] + point[1]*A[1][2] + point[2]*A[2][2];

  output[0] = x;
  output[1] = y;
  output[2] = z;
}

void vtkThinPlateSplineTransform::ForwardTransformPoint(const double point[3], 
							double output[3])
{
  vtkThinPlateSplineForwardTransformPoint(this, this->MatrixW, 
					  this->NumberOfPoints, 
					  this->BasisFunction,
					  point, output);
}

void vtkThinPlateSplineTransform::ForwardTransformPoint(const float point[3], 
							float output[3])
{
  vtkThinPlateSplineForwardTransformPoint(this, this->MatrixW, 
					  this->NumberOfPoints,
					  this->BasisFunction,
					  point, output);
}

//----------------------------------------------------------------------------
// calculate the thin plate spline as well as the jacobian
template<class T>
static inline void vtkThinPlateSplineForwardTransformDerivative(
					   vtkThinPlateSplineTransform *self,
					   double **W, int N,
					   double (*phi)(double, double&),
					   const T point[3], T output[3],
					   T derivative[3][3])
{
  if (N == 0)
    {
    for (int i = 0; i < 3; i++)
      {
      output[i] = point[i];
      derivative[i][0] = derivative[i][1] = derivative[i][2] = 0.0;
      derivative[i][i] = 1.0;
      }
    return;
    }

  double *C = W[N]; 
  double **A = &W[N+1];

  double dx,dy,dz;
  double p[3];
  double r, U, f, Ux, Uy, Uz;
  double x = 0, y = 0, z = 0; 
  double invSigma = 1.0/self->GetSigma();

  derivative[0][0] = derivative[0][1] = derivative[0][2] = 0;
  derivative[1][0] = derivative[1][1] = derivative[1][2] = 0;
  derivative[2][0] = derivative[2][1] = derivative[2][2] = 0;

  vtkPoints *sourceLandmarks = self->GetSourceLandmarks();

  // do the nonlinear stuff
  for(int i = 0; i < N; i++)
    {
    sourceLandmarks->GetPoint(i,p);
    dx = point[0]-p[0]; dy = point[1]-p[1]; dz = point[2]-p[2];
    r = sqrt(dx*dx + dy*dy + dz*dz);

    // get both U and its derivative and do the sigma-mangling
    U = 0;
    f = 0;
    if (r != 0)
      {
      U = phi(r*invSigma,f);
      f *= invSigma/r;
      }

    Ux = f*dx;
    Uy = f*dy;
    Uz = f*dz;

    x += U*W[i][0];
    y += U*W[i][1];
    z += U*W[i][2];

    derivative[0][0] += Ux*W[i][0];
    derivative[0][1] += Uy*W[i][0];
    derivative[0][2] += Uz*W[i][0];
    derivative[1][0] += Ux*W[i][1];
    derivative[1][1] += Uy*W[i][1];
    derivative[1][2] += Uz*W[i][1];
    derivative[2][0] += Ux*W[i][2];
    derivative[2][1] += Uy*W[i][2];
    derivative[2][2] += Uz*W[i][2];
    }

  // finish with the affine transformation
  x += C[0] + point[0]*A[0][0] + point[1]*A[1][0] + point[2]*A[2][0];
  y += C[1] + point[0]*A[0][1] + point[1]*A[1][1] + point[2]*A[2][1];
  z += C[2] + point[0]*A[0][2] + point[1]*A[1][2] + point[2]*A[2][2];

  output[0] = x;
  output[1] = y;
  output[2] = z;

  derivative[0][0] += A[0][0];
  derivative[0][1] += A[1][0];
  derivative[0][2] += A[2][0];
  derivative[1][0] += A[0][1];
  derivative[1][1] += A[1][1];
  derivative[1][2] += A[2][1];
  derivative[2][0] += A[0][2];
  derivative[2][1] += A[1][2];
  derivative[2][2] += A[2][2];
}  

void vtkThinPlateSplineTransform::ForwardTransformDerivative(
                                                  const double point[3],
						  double output[3],
						  double derivative[3][3])
{
  vtkThinPlateSplineForwardTransformDerivative(this, this->MatrixW, 
					       this->NumberOfPoints,
					       this->BasisDerivative,
					       point, output, derivative);
}

void vtkThinPlateSplineTransform::ForwardTransformDerivative(
                                                  const float point[3],
						  float output[3],
						  float derivative[3][3])
{
  vtkThinPlateSplineForwardTransformDerivative(this, this->MatrixW, 
					       this->NumberOfPoints,
					       this->BasisDerivative,
					       point, output, derivative);
}

//------------------------------------------------------------------------
void vtkThinPlateSplineTransform::PrintSelf(ostream& os, vtkIndent indent)
{
  vtkWarpTransform::PrintSelf(os,indent);
  
  os << indent << "Sigma: " << this->Sigma << "\n";
  os << indent << "Basis: " << this->GetBasisAsString() << "\n";
  os << indent << "Source Landmarks: " << this->SourceLandmarks << "\n";
  if (this->SourceLandmarks)
    {
    this->SourceLandmarks->PrintSelf(os,indent.GetNextIndent());
    }
  os << indent << "Target Landmarks: " << this->TargetLandmarks << "\n";
  if (this->TargetLandmarks)
    {
    this->TargetLandmarks->PrintSelf(os,indent.GetNextIndent());
    }
}

//----------------------------------------------------------------------------
vtkAbstractTransform *vtkThinPlateSplineTransform::MakeTransform()
{
  return vtkThinPlateSplineTransform::New(); 
}

//----------------------------------------------------------------------------
void vtkThinPlateSplineTransform::InternalDeepCopy(
				      vtkAbstractTransform *transform)
{
  vtkThinPlateSplineTransform *t = (vtkThinPlateSplineTransform *)transform;

  this->SetInverseTolerance(t->InverseTolerance);
  this->SetInverseIterations(t->InverseIterations);
  this->SetSigma(t->Sigma);
  this->SetBasis(t->GetBasis());
  this->SetSourceLandmarks(t->SourceLandmarks);
  this->SetTargetLandmarks(t->TargetLandmarks);

  if (this->InverseFlag != t->InverseFlag)
    {
    this->InverseFlag = t->InverseFlag;
    this->Modified();
    }
}

//------------------------------------------------------------------------
// a very basic radial basis function
static double RBFr(double r)
{
  return r;
}

// calculate both phi(r) its derivative wrt r
static double RBFDRr(double r, double &dUdr)
{
  dUdr = 1;
  return r;
}

//------------------------------------------------------------------------
// the standard 2D thin plate spline basis function
static double RBFr2logr(double r)
{
  if (r)
    {
    return r*r*log(r);
    }
  else
    {
    return 0;
    }
}

// calculate both phi(r) its derivative wrt r
static double RBFDRr2logr(double r, double &dUdr)
{
  if (r)
    {
    double tmp = log(r);
    dUdr = r*(1+2*tmp);
    return r*r*tmp;
    }
  else
    {
    dUdr = 0;
    return 0;
    }
}

//------------------------------------------------------------------------
void vtkThinPlateSplineTransform::SetBasis(int basis)
{
  if (basis == this->Basis)
    {
    return;
    }

  switch (basis)
    {
    case VTK_RBF_CUSTOM:
      break;
    case VTK_RBF_R:
      this->BasisFunction = &RBFr;
      this->BasisDerivative = &RBFDRr;
      break;
    case VTK_RBF_R2LOGR:
      this->BasisFunction = &RBFr2logr;
      this->BasisDerivative = &RBFDRr2logr;
      break;
    default:
      vtkErrorMacro(<< "SetBasisFunction: Unrecognized basis function");
      break;
    }

  this->Basis = basis;
  this->Modified();
}  

//------------------------------------------------------------------------
const char *vtkThinPlateSplineTransform::GetBasisAsString()
{
  switch (this->Basis)
    {
    case VTK_RBF_CUSTOM:
      return "Custom";
    case VTK_RBF_R:
      return "R";
    case VTK_RBF_R2LOGR:
      return "R2LogR";
     }
  return "Unknown";
}








