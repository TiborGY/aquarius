/* Copyright (c) 2013, Devin Matthews
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following
 * conditions are met:
 *      * Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *      * Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL DEVIN MATTHEWS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE. */

#ifndef _AQUARIUS_CC_1EDENSITY_HPP_
#define _AQUARIUS_CC_1EDENSITY_HPP_

#include "operator/excitationoperator.hpp"
#include "operator/deexcitationoperator.hpp"
#include "operator/1eoperator.hpp"

namespace aquarius
{
namespace cc
{

template <typename U>
class OneElectronDensity : public op::OneElectronOperator<U>
{
    public:
        /*
         * Form the SCF density
         */
        OneElectronDensity(const scf::UHF<U>& uhf)
        : op::OneElectronOperator<U>(uhf)
        {
            int N = uhf.getMolecule().getNumOrbitals();
            int nI = uhf.getMolecule().getNumAlphaElectrons();
            int ni = uhf.getMolecule().getNumBetaElectrons();
            int nA = N-nI;
            int na = N-ni;

            int NN[] = {NS,NS};
            int sizeII[] = {nI,nI};
            int sizeii[] = {ni,ni};
            int sizeAI[] = {nA,nI};
            int sizeai[] = {na,ni};

            const tensor::DistTensor<U>& CA = uhf.getCA();
            const tensor::DistTensor<U>& Ca = uhf.getCa();
            const tensor::DistTensor<U>& CI = uhf.getCI();
            const tensor::DistTensor<U>& Ci = uhf.getCi();

            tensor::DistTensor<U> DIJ(this->ctf, 2, sizeII, NN);
            tensor::DistTensor<U> Dij(this->ctf, 2, sizeii, NN);
            tensor::DistTensor<U> DAI(this->ctf, 2, sizeAI, NN);
            tensor::DistTensor<U> Dai(this->ctf, 2, sizeai, NN);

            DIJ["IJ"] = CI["pI"]*CI["pJ"];
            Dij["ij"] = Ci["pi"]*Ci["pj"];
            DAI["AI"] = CA["pA"]*CI["pI"];
            Dai["ai"] = Ca["pa"]*Ci["pi"];

            this->ab(1,0,1,0)["AB"] = DAI["AI"]*DAI["BI"];
            this->ab(0,0,0,0)["ab"] = Dai["ai"]*Dai["bi"];

            this->ij(0,1,0,1)["IJ"] = DIJ["IK"]*DIJ["JK"];
            this->ij(0,0,0,0)["ij"] = Dij["ik"]*Dij["jk"];

            this->ai(1,0,0,1)["AI"] = DAI["AJ"]*DIJ["IJ"];
            this->ai(0,0,0,0)["ai"] = Dai["aj"]*Dij["ij"];
        }

        /*
         * Form the unrelaxed CCSD density
         */
        OneElectronDensity(const op::ExponentialOperator<U,2>& T)
        : op::OneElectronOperator<U>(T.getSCF(), false)
        {
            this->ai["ai"] = T(1)["ai"];
        }

        /*
         * Form the partial perturbed CCSD Density
         */
        OneElectronDensity(const op::DeexcitationOperator<U,2>& L,
                           const op::ExponentialOperator<U,2>& T,
                           const op::ExcitationOperator<U,2>& TA)
        : op::OneElectronOperator<U>(L.getSCF(), false)
        {
            op::OneElectronOperator<U> I(this->uhf);

            tensor::SpinorbitalTensor< tensor::DistTensor<U> >& IIJ = I.getIJ();
            tensor::SpinorbitalTensor< tensor::DistTensor<U> >& IAB = I.getAB();

            IAB["ab"] = 0.5*T(2)["aemn"]*L(2)["mnbe"];
            IIJ["ij"] = 0.5*T(2)["efim"]*L(2)["jmef"];

            this->ab["ab"] += TA(1)["am"]*L(1)["mb"];
            this->ab["ab"] += 0.5*TA(2)["aemn"]*L(2)["mnbe"];

            this->ij["ij"] -= TA(1)["ei"]*L(1)["je"];
            this->ij["ij"] -= 0.5*TA(2)["efim"]*L(2)["jmef"];

            this->ai["ai"] += TA(1)["ai"];
            this->ai["ai"] += TA(2)["aeim"]*L(1)["me"];
            this->ai["ai"] += this->ij["mi"]*T(1)["am"];
            this->ai["ai"] -= this->ab["ae"]*T(1)["ei"];
            this->ai["ai"] -= IIJ["mi"]*TA(1)["am"];
            this->ai["ai"] -= IAB["ae"]*TA(1)["ei"];
        }

        /*
         * Form the relaxed CCSD density
         */
        OneElectronDensity(const op::DeexcitationOperator<U,2>& L,
                           const op::ExponentialOperator<U,2>& T)
        : op::OneElectronOperator<U>(L.getSCF(), false)
        {
            this->ia["ia"] += L(1)["ia"];

            this->ab["ab"] += 0.5*T(2)["aemn"]*L(2)["mnbe"];

            this->ij["ij"] -= T(1)["ei"]*L(1)["je"];
            this->ij["ij"] -= 0.5*T(2)["efim"]*L(2)["jmef"];

            this->ai["ai"] += T(1)["ai"];
            this->ai["ai"] += T(2)["aeim"]*L(1)["me"];
            this->ai["ai"] += this->ij["mi"]*T(1)["am"];
            this->ai["ai"] -= this->ab["ae"]*T(1)["ei"];

            this->ab["ab"] += T(1)["am"]*L(1)["mb"];
        }

        /*
         * Form the relaxed perturbed CCSD Density
         */
        OneElectronDensity(const op::DeexcitationOperator<U,2>& L,
                           const op::DeexcitationOperator<U,2>& LA,
                           const op::ExponentialOperator<U,2>& T,
                           const op::ExcitationOperator<U,2>& TA)
        : op::OneElectronOperator<U>(L.getSCF(), false)
        {
            op::OneElectronOperator<U> I(this->uhf);

            tensor::SpinorbitalTensor< tensor::DistTensor<U> >& IIJ = I.getIJ();
            tensor::SpinorbitalTensor< tensor::DistTensor<U> >& IAB = I.getAB();

            this->ia["ia"] += LA(1)["ia"];

            this->ab["ab"] += 0.5*T(2)["aemn"]*LA(2)["mnbe"];

            this->ij["ij"] -= T(1)["ei"]*LA(1)["je"];
            this->ij["ij"] -= 0.5*T(2)["efim"]*LA(2)["jmef"];

            this->ai["ai"] += T(1)["ai"];
            this->ai["ai"] += T(2)["aeim"]*LA(1)["me"];
            this->ai["ai"] += this->ij["mi"]*T(1)["am"];
            this->ai["ai"] -= this->ab["ae"]*T(1)["ei"];

            this->ab["ab"] += T(1)["am"]*LA(1)["mb"];

            IAB["ab"]  = TA(1)["am"]*L(1)["mb"];
            IAB["ab"] += 0.5*TA(2)["aemn"]*L(2)["mnbe"];

            this->ab["ab"] += IAB["ab"];
            this->ij["ij"] -= IIJ["ij"];

            IIJ["ij"]  = TA(1)["ei"]*L(1)["je"];
            IIJ["ij"] += 0.5*TA(2)["efim"]*L(2)["jmef"];

            this->ai["ai"] += TA(1)["ai"];
            this->ai["ai"] += TA(2)["aeim"]*L(1)["me"];
            this->ai["ai"] -= IIJ["mi"]*T(1)["am"];
            this->ai["ai"] -= IAB["ae"]*T(1)["ei"];

            IAB["ab"] = 0.5*T(2)["aemn"]*L(2)["mnbe"];
            IIJ["ij"] = 0.5*T(2)["efim"]*L(2)["jmef"];

            this->ai["ai"] -= IIJ["mi"]*TA(1)["am"];
            this->ai["ai"] -= IAB["ae"]*TA(1)["ei"];
        }
};

}
}

#endif
