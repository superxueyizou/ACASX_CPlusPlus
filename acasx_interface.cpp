/* *************************************************************************************
 * Copyright (C) Xueyi Zou - All Rights Reserved
 * Written by Xueyi Zou <xz972@york.ac.uk>, 2015
 * You are free to use/modify/distribute this file for whatever purpose!
 -----------------------------------------------------------------------
 |THIS FILE IS DISTRIBUTED "AS IS", WITHOUT ANY EXPRESS OR IMPLIED
 |WARRANTY. THE USER WILL USE IT AT HIS/HER OWN RISK. THE ORIGINAL
 |AUTHORS AND COPPELIA ROBOTICS GMBH WILL NOT BE LIABLE FOR DATA LOSS,
 |DAMAGES, LOSS OF PROFITS OR ANY OTHER KIND OF LOSS WHILE USING OR
 |MISUSING THIS SOFTWARE.
 ------------------------------------------------------------------------

 **************************************************************************************/


#include "acasx_interface.h"

#include <map>
#include <limits>
#include <cmath>
#include <assert.h>
#include "utils.h"
#include "lookuptable.h"
#include "double2d.h"

static LookupTable* lookupTable;

void calculateEntryTimeDistributionDTMC(Double3D& ownshipLoc, Double3D& ownshipVel,Double3D& intruderLoc, Double3D& intruderVel, map<int, double>& entryTimeDistribution);
void calculateQValuesMap(Double3D& ownshipLoc, Double3D& ownshipVel,Double3D& intruderLoc, Double3D& intruderVel, const int lastRA, map<int, double>& entryTimeDistribution, map<int, double>& qValueMap);

//used only once to load the lookup table, which is time consuming.
void initialize()
{
    lookupTable=&LookupTable::getInstance();
}

//for only one threat, passing in the ownship and the intruder's positions and velocities and the recent Resolution Advice (RA) issured by the ownship
int ACASX_SingleThreat(Double3D& ownshipLoc, Double3D& ownshipVel, Double3D& intruderLoc, Double3D& intruderVel, int lastRA)
{
    map<int, double> entryTimeDistribution;
    map<int, double> qValueMap;

    Double2D horiDistVector( intruderLoc.x()-ownshipLoc.x(), intruderLoc.z()-ownshipLoc.z());
    double r=horiDistVector.length();
    double h=(intruderLoc.y()-ownshipLoc.y());

    if(fabs(h)<=MDP_UPPER_H && r<=DTMC_UPPER_R)//if collision avoidance needed?
    {
        calculateEntryTimeDistributionDTMC( ownshipLoc, ownshipVel, intruderLoc, intruderVel, entryTimeDistribution);//calculate the Entry Time Distribution
        calculateQValuesMap( ownshipLoc, ownshipVel,  intruderLoc, intruderVel,lastRA, entryTimeDistribution, qValueMap);//calculate q-values
    }
    else
    {
        return 0;
    }

    //find the best actioncode (RA)
    double maxQValue=qValueMap.begin()->second;
    int bestActionCode=qValueMap.begin()->first;
    for (auto entry : qValueMap)
    {
        double value=entry.second;
        if(value-maxQValue>=0.0001)
        {
            maxQValue=value;
            bestActionCode=entry.first;
        }
    }
    lastRA=bestActionCode;
    return lastRA;
}

//for multiple threat, passing in the ownship and the intruders' positions and velocities as vectors, and the recent Resolution Advice (RA) issured by the ownship
int ACASX_MultiThreats(Double3D& ownshipLoc, Double3D& ownshipVel, vector<Double3D>& intrudersLocs, vector<Double3D>& intrudersVels, int lastRA)
{
    if(intrudersLocs.size()!=intrudersVels.size())
    {
        cerr<<"the size of intrudersLocs should be equal to the size of intrudersVels!"<<endl;
        abort();
    }

    map<int, double> qValuesMap_total;

    map<int, double> entryTimeDistribution;
    map<int, double> qValuesMap_single;

    //using value fusing, iterate through every intruders and summing the values
    for(unsigned int i=0; i<intrudersLocs.size(); ++i)
    {
        Double2D horiDistVector( intrudersLocs.at(i).x()-ownshipLoc.x(), intrudersLocs.at(i).z()-ownshipLoc.z());
        double r=horiDistVector.length();
        double h=(intrudersLocs.at(i).y()-ownshipLoc.y());

        if(fabs(h)<=MDP_UPPER_H && r<=DTMC_UPPER_R)// if collision avoidance needed?
        {
            calculateEntryTimeDistributionDTMC( ownshipLoc, ownshipVel, intrudersLocs.at(i), intrudersVels.at(i), entryTimeDistribution);
            calculateQValuesMap( ownshipLoc, ownshipVel,  intrudersLocs.at(i), intrudersVels.at(i), lastRA, entryTimeDistribution, qValuesMap_single);
            for (auto entry : qValuesMap_single)
            {
                auto iPairFound=qValuesMap_total.find(entry.first);
                if(iPairFound!=qValuesMap_total.end())
                {
                    double a=qValuesMap_total[entry.first];
                    cout<<a<<endl;
                    double b=qValuesMap_single[entry.first];
                    qValuesMap_total[entry.first]=min(a, b); // approach 1: min-max
//					qValuesMap_total[entry.first]=(a+b); // approach 2: value summation
                }
                else
                {
                    qValuesMap_total[entry.first] = qValuesMap_single[entry.first];
                }
            }
        }
        else
        {
            continue;
        }
    }


    //find the best actioncode (RA)
    double maxQValue=qValuesMap_total.begin()->second;
    int bestActionCode=qValuesMap_total.begin()->first;
    for (auto entry : qValuesMap_total)
    {
        double value=entry.second;
        if(value-maxQValue>=0.0001)
        {
            maxQValue=value;
            bestActionCode=entry.first;
        }
    }
    lastRA=bestActionCode;
    return lastRA;
}



//for getting the target velocity of a Resolution Advice (RA)
double getActionV(int actionCode)
{
    return  acasx::getActionV(actionCode);
}

//for getting the target acceleration of a Resolution Advice (RA)
double getActionA(int actionCode)
{
    return  acasx::getActionA(actionCode);
}


void calculateEntryTimeDistributionDTMC(Double3D& ownshipLoc, Double3D& ownshipVel,Double3D& intruderLoc, Double3D& intruderVel, map<int, double>& entryTimeDistribution)
{
    entryTimeDistribution.clear();

    Double2D horiDistVector (intruderLoc.x()-ownshipLoc.x(), intruderLoc.z()-ownshipLoc.z());
    Double2D horiVelVector (intruderVel.x()-ownshipVel.x(), intruderVel.z()-ownshipVel.z());
    double r=horiDistVector.length();
    double rv=horiVelVector.length();
    double alpha=horiVelVector.angle()-horiDistVector.angle();
    if(alpha> M_PI)
    {
        alpha= -2*M_PI +alpha;
    }
    if(alpha<-M_PI)
    {
        alpha=2*M_PI+alpha;
    }
    double theta = 180.0*alpha/M_PI;

    double rRes=DTMC_R_RES;
    double rvRes=DTMC_RV_RES;
    double thetaRes=DTMC_THETA_RES;

    int nr = DTMC_R_NUM;
    int nrv = DTMC_RV_NUM;
    int ntheta = DTMC_THETA_NUM;

    vector< pair<int, double> > entryTimeMapProbs;

    assert (r<=DTMC_UPPER_R);
    assert (rv<=DTMC_UPPER_RV);
    assert (theta>=-180 && theta<=180);

    // the following uses the method of linear interpolation for approximating the states
    int rIdxL = (int)floor(r/rRes);
    int rvIdxL = (int)floor(rv/rvRes);
    int thetaIdxL = (int)floor(theta/thetaRes);
    for(int i=0;i<=1;i++)
    {
        int rIdx = (i==0? rIdxL : rIdxL+1);
        int rIdxP= rIdx< 0? 0: (rIdx>nr? nr : rIdx);
        for(int j=0;j<=1;j++)
        {
            int rvIdx = (j==0? rvIdxL : rvIdxL+1);
            int rvIdxP= rvIdx<0? 0: (rvIdx>nrv? nrv : rvIdx);
            for(int k=0;k<=1;k++)
            {
                int thetaIdx = (k==0? thetaIdxL : thetaIdxL+1);
                int thetaIdxP= thetaIdx<-ntheta? -ntheta: (thetaIdx>ntheta? ntheta : thetaIdx);

                int approxUStateOrder = State_UnCtrl_CalOrder(rIdxP, rvIdxP, thetaIdxP);
                double probability= (1-fabs(rIdx-r/rRes))*(1-fabs(rvIdx-rv/rvRes))*(1-fabs(thetaIdx-theta/thetaRes));
                for(int t=0;t<=TIME_HORIZON;++t)
                {
                    entryTimeMapProbs.push_back( pair<int, double>(t, probability*lookupTable->entryTimeDistributionArr.at(t*(lookupTable->numUStates)+ approxUStateOrder) ) );
                }

            }
        }
    }

    //merge entries with the same keys by summing up their values
    double entryTimeLessThanTProb=0;
    for(auto entryTime_prob :entryTimeMapProbs)
    {
        int StateOrder=entryTime_prob.first;
        auto iPairFound = entryTimeDistribution.find(StateOrder);
        if(iPairFound != entryTimeDistribution.end())
        {
            entryTimeDistribution[StateOrder]+= entryTime_prob.second;
        }
        else
        {
            entryTimeDistribution.insert(entryTime_prob);
        }
        entryTimeLessThanTProb+=entryTime_prob.second;
    }
    entryTimeDistribution.insert( pair<int, double>(TIME_HORIZON+1, 1-entryTimeLessThanTProb) );

}



void calculateQValuesMap(Double3D& ownshipLoc, Double3D& ownshipVel,Double3D& intruderLoc, Double3D& intruderVel, const int lastRA,  map<int, double>& entryTimeDistribution, map<int, double>& qValueMap)
{
    qValueMap.clear();

    double h=(intruderLoc.y()-ownshipLoc.y());
    double oVy=ownshipVel.y();
    double iVy=intruderVel.y();

    double hRes=MDP_H_RES;
    double oVRes=MDP_OV_RES;
    double iVRes=MDP_IV_RES;
    int nh = MDP_H_NUM;
    int noVy = MDP_OVY_NUM;
    int niVy = MDP_IVY_NUM;

    assert (fabs(h)<=MDP_UPPER_H);
    assert (fabs(oVy)<=MDP_UPPER_VY);
    assert (fabs(iVy)<=MDP_UPPER_VY);

    vector< pair<int, double> > actionMapValues;

    // the following uses the method of linear interpolation for approximating the states
    int hIdxL = (int)floor(h/hRes);
    int oVyIdxL = (int)floor(oVy/oVRes);
    int iVyIdxL = (int)floor(iVy/iVRes);
    for(int i=0;i<=1;i++)
    {
        int hIdx = (i==0? hIdxL : hIdxL+1);
        int hIdxP= hIdx< -nh? -nh: (hIdx>nh? nh : hIdx);
        for(int j=0;j<=1;j++)
        {
            int oVyIdx = (j==0? oVyIdxL : oVyIdxL+1);
            int oVyIdxP= oVyIdx<-noVy? -noVy: (oVyIdx>noVy? noVy : oVyIdx);
            for(int k=0;k<=1;k++)
            {
                int iVyIdx = (k==0? iVyIdxL : iVyIdxL+1);
                int iVyIdxP= iVyIdx<-niVy? -niVy: (iVyIdx>niVy? niVy : iVyIdx);

                int approxCStateOrder = State_Ctrl_CalOrder(hIdxP, oVyIdxP, iVyIdxP, lastRA);
                double probability= (1-fabs(hIdx*1.0-h/hRes))*(1-fabs(oVyIdx-oVy/oVRes))*(1-fabs(iVyIdx-iVy/iVRes));

                for(auto entryTime_prob :entryTimeDistribution)
                {
                    int t=entryTime_prob.first;
                    double entryTimeProb= entryTime_prob.second;
                    int index =lookupTable->indexArr.at( t*(lookupTable->numCStates)+ approxCStateOrder );
                    int numActions = lookupTable->indexArr.at( t*(lookupTable->numCStates)+approxCStateOrder+1 ) - index;

                    for (int n=0;n<numActions;n++)
                    {
                        double qValue= lookupTable->costArr.at(index+n);
                        int actionCode= lookupTable->actionArr.at(index+n);
                        actionMapValues.push_back( pair<int, double>(actionCode,probability*entryTimeProb*qValue) );
                    }
                }
            }
        }
    }


    //merge entries with the same keys by summing up their values
    for(auto action_value :actionMapValues)
    {
        int action=action_value.first;
        auto iFound = qValueMap.find(action);
        if(iFound!= qValueMap.end())
        {
            qValueMap[action] = qValueMap[action]+ action_value.second;
        }
        else
        {
            qValueMap.insert( action_value);
        }
    }


}

//int main()
//{
//    initialize();
//    Double3D ownshipLoc (0.0, 500.0, 0.0);
//    Double3D ownshipVel (203, 0.0, 0.0);
//    Double3D intruderLoc (4034, 547.0, 0.0);
//    Double3D intruderVel (-185, 0.0, 0.0);
//    int lastRA = 0;
//    for(lastRA=0; lastRA<7; ++lastRA)
//    {
//        int returnResult = ACASX_SingleThreat( ownshipLoc, ownshipVel,  intruderLoc,  intruderVel, lastRA);
//        cout<<returnResult<<endl; // 4 is expected
//    }

//    return 0;
//}
