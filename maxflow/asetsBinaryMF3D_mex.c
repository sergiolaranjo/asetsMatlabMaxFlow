/*  Martin Rajchl, Imperial College London, 2015
 *
 *  Re-implementation with of [1]
 *
 *  [1] Yuan, J.; Bae, E.; Tai, X,-C.;
 *      A study on continuous max-flow and min-cut approaches
 *      IEEE CVPR, 2010
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <mex.h>
#include <math.h>
#include <time.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define ABS(x) ( (x) > 0.0 ? x : -(x) )

void runMaxFlow( float *alpha, float *Cs, float *Ct,
        int Nx, int Ny, int Nz, int nLab, int maxIt,
        float errbound, float cc, float steps,
        float *u, float *cvg, int *itNum);

void init(float *Cs, float *Ct, float *ps, float *pt, float *u, int Nx, int Ny, int Nz);

void updateP1(float *gk, float *dv, float *ps, float *pt, float *u, int Nx, int Ny, int Nz, float cc);
void updatePX(float *gk, float *bx, int Nx, int Ny, int Nz, float steps);
void updatePY(float *gk, float *by, int Nx, int Ny, int Nz, float steps);
void updatePZ(float *gk, float *bz, int Nx, int Ny, int Nz, float steps);
void projStep(float *bx, float *by, float *bz, float *alpha, float *gk, int Nx, int Ny, int Nz);
void updateBX(float *bx, float *gk, int Nx, int Ny, int Nz);
void updateBY(float *by, float *gk, int Nx, int Ny, int Nz);
void updateBZ(float *bz, float *gk, int Nx, int Ny, int Nz);
float updateDIVPSPTU(float *dv, float *bx, float *by, float *bz, float *ps, float *pt, float *Cs, float *Ct, float *u, int Nx, int Ny, int Nz, float cc);


extern void mexFunction(int iNbOut, mxArray *pmxOut[],
        int iNbIn, const mxArray *pmxIn[])
{
    /* vars in */
    float *alpha, *Cs, *Ct, *pars;
    int Nx, Ny, Nz, nLab, maxIt;
    float errBound, cc, steps;
    
    /* vars out */
    float *u, *cvg;
    int *itNum;
    double *runTime;
    
    int dim[4];
    int nDim;
    
    /* others */
    time_t  start_time, end_time;
    
    /* compute Max-Flow */
    start_time = clock();
    
    /* Inputs */
    Cs = mxGetData(pmxIn[0]);             /* bound of source flows */
    Ct = mxGetData(pmxIn[1]);             /* bound of sink flows */
    alpha = mxGetData(pmxIn[2]);        /* penalty parameters */
    pars = mxGetData(pmxIn[3]);  /* Vector of parameters */
    
    /*
     *pfVecParameters Setting
     * [0] : number of columns
     * [1] : number of rows
     * [2] : number of slices
     * [4] : the maximum iteration number
     * [5] : error criterion
     * [6] : cc for the step-size of ALM
     * [7] : steps for the step-size of projected-gradient of p
     */
    
    /* pars */
    Ny = (int) pars[0];
    Nx = (int) pars[1];
    Nz = (int) pars[2];
    maxIt = (int) pars[3];
    errBound = (float) pars[4];
    cc = (float) pars[5];
    steps = (float) pars[6];
    
    nLab = 1;
    
    /* Outputs */
    /* outputs the computed u(x)  */
    dim[0] = Ny;
    dim[1] = Nx;
    dim[2] = Nz;
    nDim = 3;
    
    pmxOut[0] = mxCreateNumericArray(nDim,(const int*)dim,mxSINGLE_CLASS,mxREAL);
    u = mxGetData(pmxOut[0]);
    
    /* outputs the convergence rate  */
    nDim = 2;
    dim[0] = maxIt;
    dim[1] = 1;
    pmxOut[1] = mxCreateNumericArray(nDim,(const int*)dim,mxSINGLE_CLASS,mxREAL);
    cvg = mxGetData(pmxOut[1]);
    
    /* outputs the iteration number  */
    nDim = 2;
    dim[0] = 1;
    dim[1] = 1;
    pmxOut[2] = mxCreateNumericArray(nDim,(const int*)dim,mxUINT16_CLASS,mxREAL);
    itNum = mxGetData(pmxOut[2]);
    
    /* outputs the computation time  */
    nDim = 2;
    dim[0] = 1;
    dim[1] = 1;
    pmxOut[3] = mxCreateNumericArray(nDim,(const int*)dim,mxSINGLE_CLASS,mxREAL);
    runTime = mxGetData(pmxOut[3]);
    
    
    /* compute Max-Flow */
    start_time = clock();
    
    runMaxFlow(alpha, Cs, Ct,
            Nx, Ny, Nz, nLab, maxIt, errBound, cc, steps,
            u, cvg, itNum);
    
    
    end_time = clock();
    
    runTime[0] = difftime(end_time, start_time)/1000000;
    mexPrintf("binary max flow 3D: number of iterations = %i; time = %.4f sec\n",itNum[0],runTime[0]);
    
}


void runMaxFlow( float *alpha, float *Cs, float *Ct,
        int Nx, int Ny, int Nz, int nLab, int maxIt,
        float errBound, float cc, float steps,
        float *u, float *cvg, int *itNum){
    
    
    float   *bx, *by, *bz, *dv, *gk, *ps, *pt;
    int i;
    float total_err;
    
    /* alloc buffers */
    bx = (float *) calloc( (unsigned)((Nx+1)*Ny*Nz), sizeof(float) );
    by = (float *) calloc( (unsigned)(Nx*(Ny+1)*Nz), sizeof(float) );
    bz = (float *) calloc( (unsigned)(Nx*Ny*(Nz+1)), sizeof(float) );
    dv = (float *) calloc( (unsigned)(Nx*Ny*Nz), sizeof(float) );
    gk = (float *) calloc( (unsigned)(Nx*Ny*Nz), sizeof(float) );
    ps = (float *) calloc( (unsigned)(Nx*Ny*Nz), sizeof(float) );
    pt = (float *) calloc( (unsigned)(Nx*Ny*Nz), sizeof(float) );
    if (!(bx || by || bz || dv || gk || ps || pt))
        mexPrintf("malloc error.\n");
    
    init(Cs, Ct, ps, pt, u, Nx, Ny, Nz);
    
    
    /* iterate */
    i = 0;
    for (i = 0; i < maxIt; i++){
        
        /* update the spatial flow field p(x) = (bx(x),by(x),bz(x)) */
        updateP1(gk, dv, ps, pt, u, Nx, Ny, Nz, cc);
        updatePX(gk, bx, Nx, Ny, Nz, steps);
        updatePY(gk, by, Nx, Ny, Nz, steps);
        updatePZ(gk, bz, Nx, Ny, Nz, steps);
        
        /* projection step to make |p(x,i)| <= alpha(x)*/
        projStep(bx, by, bz, alpha, gk, Nx, Ny, Nz);
        
        /* update the component bx, by, bz */
        updateBX(bx, gk, Nx, Ny, Nz);
        updateBY(by, gk, Nx, Ny, Nz);
        updateBZ(bz, gk, Nx, Ny, Nz);
        
        /* update ps(x)/pt(x) and the multiplier/labeling functions u(x) */
        total_err = updateDIVPSPTU(dv, bx, by, bz, ps, pt, Cs, Ct, u, Nx, Ny, Nz, cc);
        
        /* evaluate the convergence error */
        cvg[i] = total_err / (float)(Nx*Ny*Nz); 
        /*mexPrintf("it= %d, cvg = %f\n", i,cvg[i] ); */
        
        /* check if converged */
        if (cvg[i] <= errBound)
            break;
        
    }
    /* update iteration number */
    itNum[0] = i;
    
    /* free mem */
    free( (float *) bx );
    free( (float *) by );
    free( (float *) bz );
    free( (float *) dv );
    free( (float *) gk );
    free( (float *) ps );
    free( (float *) pt );
    
}

void init(float *Cs, float *Ct, float *ps, float *pt, float *u, int Nx, int Ny, int Nz){
    /* init */
    int x, y, z;
    
    for (x=0; x < Nx; x++){
        for (y=0; y < Ny; y++){
            for (z=0; z < Nz; z++){
                
                int g_idx = z*Nx*Ny + x*Ny + y;
                
                if(Cs[g_idx] - Ct[g_idx] >= 0){
                    u[g_idx] = 1.0f;
                }
                ps[g_idx] = MIN(Cs[g_idx], Ct[g_idx]);
                pt[g_idx] = ps[g_idx];
                
            }
        }
    }
}

void updateP1(float *gk, float *dv, float *ps, float *pt, float *u, int Nx, int Ny, int Nz, float cc){
    
    int x = 0;
    int y = 0;
    int z = 0;
    
    for (z=0; z < Nz; z++){
        for (x=0; x < Nx; x++){
            for (y=0; y < Ny; y++){
                
                int g_idx = z*Nx*Ny + x*Ny + y;
                
                gk[g_idx] = dv[g_idx] - (ps[g_idx]
                        - pt[g_idx] + u[g_idx]/cc);
                
            }
        }
    }
}

void updatePX(float *gk, float *bx, int Nx, int Ny, int Nz, float steps){
    
    int x = 0;
    int y = 0;
    int z = 0;
    for (z=0; z < Nz; z++){
        for (x=1; x < Nx; x++){
            for (y=0; y < Ny; y++){
                
                
                int g_idx = z*Nx*Ny + x*Ny + y;
                
                bx[g_idx] = steps*(gk[g_idx] - gk[g_idx-Ny]) + bx[g_idx];
                
            }
        }
    }
    
}

void updatePY(float *gk, float *by, int Nx, int Ny, int Nz, float steps){
    
    int x = 0;
    int y = 0;
    int z = 0;
    for (z=0; z < Nz; z++){
        for(x = 0; x < Nx; x ++){
            for(y = 1; y < Ny; y++){
                
                
                int g_idx = z*Nx*Ny + x*Ny + y;
                
                by[g_idx] = steps*(gk[g_idx] - gk[g_idx-1]) + by[g_idx];
                
            }
        }
    }
}

void updatePZ(float *gk, float *bz, int Nx, int Ny, int Nz, float steps){
    
    int x = 0;
    int y = 0;
    int z = 0;
    for (z=1; z < Nz; z++){
        for(x = 0; x < Nx; x ++){
            for(y = 0; y < Ny; y++){
                
                
                int g_idx = z*Nx*Ny + x*Ny + y;
                
                bz[g_idx] = steps*(gk[g_idx] - gk[g_idx-(Nx*Ny)]) + bz[g_idx];
            }
        }
    }
}


void projStep(float *bx, float *by, float *bz, float *alpha, float *gk, int Nx, int Ny, int Nz){
    
    float fpt;
    int x = 0;
    int y = 0;
    int z = 0;
    for (z=0; z < Nz; z++){
        for (x=0; x< Nx; x++){
            for (y=0; y< Ny; y++){
                
                
                int g_idx = z*Nx*Ny + x*Ny + y;
                
                if( alpha[g_idx] <= 0 ){
                    mexErrMsgTxt("alpha(x) must be positive. Exiting...");
                }
                
                fpt = sqrt((pow(bx[g_idx+Ny],2) + pow(bx[g_idx],2) +
                        pow(by[g_idx+1],2) + pow(by[g_idx],2) +
                        pow(bz[g_idx+(Nx*Ny)],2) + pow(bz[g_idx],2)
                        )*0.5);
                
                if (fpt > alpha[g_idx])
                    fpt = fpt / alpha[g_idx];
                else
                    fpt = 1;
                
                gk[g_idx] = 1/fpt;
            }
        }
    }
}

void updateBX(float *bx, float *gk, int Nx, int Ny, int Nz){
    
    int g_idx;
    int l_idx;
    int x = 0;
    int y = 0;
    int z = 0;
    for (z=0; z < Nz; z++){
        for (x=1; x< Nx; x++){
            for (y=0; y< Ny; y++){
                
                
                g_idx = z*Nx*Ny + x*Ny + y;
                
                bx[g_idx] = (gk[g_idx] + gk[g_idx-Ny])
                *0.5*bx[g_idx];
            }
        }
    }
}

void updateBY(float *by, float *gk, int Nx, int Ny, int Nz){
    
    int g_idx;
    int l_idx;
    int x = 0;
    int y = 0;
    int z = 0;
    for (z=0; z < Nz; z++){
        for (x=0; x<Nx; x++){
            for (y=1; y< Ny; y++){
                
                
                g_idx = z*Nx*Ny + x*Ny + y;
                
                by[g_idx] = 0.5*(gk[g_idx-1] + gk[g_idx])
                *by[g_idx];
            }
        }
    }
}
void updateBZ(float *bz, float *gk, int Nx, int Ny, int Nz){
    
    int g_idx;
    int l_idx;
    int x = 0;
    int y = 0;
    int z = 0;
    for (z=1; z < Nz; z++){
        for (x=0; x<Nx; x++){
            for (y=0; y< Ny; y++){
                
                
                g_idx = z*Nx*Ny + x*Ny + y;
                
                bz[g_idx] = 0.5*(gk[g_idx-(Nx*Ny)] + gk[g_idx])
                *bz[g_idx];
            }
        }
    }
}
float updateDIVPSPTU(float *dv, float *bx, float *by, float *bz, float *ps, float *pt, float *Cs, float *Ct, float *u, int Nx, int Ny, int Nz, float cc){
    
    float fpt;
    float erru = 0;
    int g_idx;
    int l_idx;
    int x = 0;
    int y = 0;
    int z = 0;
    for (z=0; z < Nz; z++){
        for (x=0; x< Nx; x++){
            for (y=0; y< Ny; y++){
                
                g_idx = z*Nx*Ny + x*Ny + y;
                
                /* update the divergence field dv(x,l)  */
                dv[g_idx] = by[g_idx+1] - by[g_idx] +
                        bx[g_idx+Ny] - bx[g_idx] +
                        bz[g_idx+(Nx*Ny)] - bz[g_idx];
                
                /* update the source flows ps */
                fpt = pt[g_idx] - u[g_idx]/cc + dv[g_idx] + 1/cc;
                fpt = MIN(fpt, Cs[g_idx]);
                ps[g_idx] = fpt;
                
                /* update the sink flows ps */
                fpt = ps[g_idx] + u[g_idx]/cc - dv[g_idx];
                fpt = MIN(fpt, Ct[g_idx]);
                pt[g_idx] = fpt;
                
                /* update the multiplier u */
                fpt = cc*(pt[g_idx] + dv[g_idx] - ps[g_idx]);
                erru += ABS(fpt);
                
                u[g_idx] -= fpt;
                
            }
        }
    }
    return erru;
}


