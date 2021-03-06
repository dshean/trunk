//##########################################################################
//#                                                                        #
//#                               CCLIB                                    #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU Library General Public License as       #
//#  published by the Free Software Foundation; version 2 of the License.  #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
//#                                                                        #
//##########################################################################

#include "KdTree.h"

//system
#include <algorithm>

using namespace CCLib;

KDTree::KDTree()
	: m_root(0)
	, associatedCloud(0)
	, m_cellCount(0)
{
}

KDTree::~KDTree()
{
    deleteSubTree(m_root);
}

bool KDTree::buildFromCloud(GenericIndexedCloud *cloud, GenericProgressCallback *progressCb)
{
    unsigned i, cloudsize=cloud->size();

    m_indexes.clear();
    m_cellCount = 0;
	associatedCloud = 0;

    if(cloudsize == 0)
        return false;

	try
	{
		m_list.resize(cloudsize);
	}
	catch (.../*const std::bad_alloc&*/) //out of memory
	{
		return false;
	}

	associatedCloud = cloud;

	for(i=0; i<cloudsize; i++)
    {
        m_list[i].index = i;
        cloud->getPoint(i,m_list[i].point);
    }

    if(progressCb)
    {
        progressCb->reset();
        progressCb->setInfo("Building KD-tree");
        progressCb->start();
    }

    m_root = buildSubTree(0, cloudsize-1, (KdCell*)0, m_cellCount, progressCb);

    if(progressCb)
        progressCb->stop();

    //if the tree building has failed (memory issues)
    if(!m_root)
    {
        associatedCloud = 0;
        m_cellCount = 0;
        return false;
    }

	try
	{
		m_indexes.resize(cloudsize);
	}
	catch (.../*const std::bad_alloc&*/) //out of memory
	{
        associatedCloud = 0;
        m_cellCount = 0;
        return false;
    }
    for(i=0; i<cloudsize; i++)
        m_indexes[i]=m_list[i].index;
    m_list.clear();

    return true;
}


void KDTree::deleteSubTree(KdCell *cell)
{
    if(!cell)
        return;

    deleteSubTree(cell->leSon);
    deleteSubTree(cell->gSon);
    delete cell;
	assert(m_cellCount>0);
    m_cellCount--;
}

KDTree::KdCell* KDTree::buildSubTree(unsigned first, unsigned last, KdCell* father, unsigned &nbBuildCell, GenericProgressCallback *progressCb)
{
    KdCell* cell = new KdCell;
    if (!cell)
        return 0;
    m_cellCount++;

    unsigned dim = (father == 0 ? 0 : ((father->cuttingDim+1) % 3));

    //Compute outside bounding box (have to be done before building the current cell sons)
    cell->father = father;
    cell->startingPointIndex = first;
    cell->nbPoints = last-first+1;
    cell->cuttingDim = dim;
    updateOutsideBoundingBox(cell);
    if(progressCb)
        progressCb->update((float)m_cellCount*100.0f/(float)(m_list.size()*2-1));

    //If there is only one point to insert, build a leaf
    if(first == last)
    {
        cell->cuttingDim = 0;
        cell->leSon = 0;
        cell->gSon = 0;
    }
    else
    {
        //sort the remaining points considering dimension dim
        if(dim == 0)
            sort(m_list.begin()+first, m_list.begin()+(last+1), ComparisonX);
        else if(dim == 1)
            sort(m_list.begin()+first, m_list.begin()+(last+1), ComparisonY);
        else if(dim == 2)
            sort(m_list.begin()+first, m_list.begin()+(last+1), ComparisonZ);
        //find the median point in the sorted tab
        unsigned split = (first+last)/2;
        const CCVector3& p = m_list[split].point;
        cell->cuttingCoordinate = p.u[dim];
        //recursively build the other two sub trees
        //trap the memory issues. At this point, none of the cell sons can be set to 0. Otherwise there has been memory allocation failure.
        cell->leSon = cell->gSon = 0;
        cell->leSon = buildSubTree(first, split, cell, nbBuildCell, progressCb);
        if(cell->leSon == 0)
        {
            deleteSubTree(cell);
            //the tree beyond the current cell will be deleted when noticing that this cell is set to 0
            return 0;
        }
        cell->gSon = buildSubTree(split+1, last, cell, nbBuildCell, progressCb);
        if(cell->gSon == 0)
        {
            deleteSubTree(cell);
            //the tree beyond the current cell will be deleted when noticing that this cell is set to 0
            return 0;
        }

    }
    //Compute inside bounding box (have to be done once sons have been built)
    updateInsideBoundingBox(cell);

    return cell;
}


bool KDTree::findNearestNeighbour(const PointCoordinateType *queryPoint,
									unsigned &nearestPointIndex,
									PointCoordinateType maxDist)
{
    KdCell *cellPtr, *prevPtr, *brotherPtr;
    unsigned i;
    int a;
    PointCoordinateType sqrdist;
    bool found = false;

    if(m_root == 0)
        return false;

    cellPtr = m_root;
    maxDist *= maxDist;
    //Go down the tree to find which cell contains the query point (at most log2(N) tests where N is the total number of points in the cloud)
    while(!(cellPtr->leSon == 0 && cellPtr->gSon == 0))
    {
        if(queryPoint[cellPtr->cuttingDim]<=cellPtr->cuttingCoordinate)
            cellPtr = cellPtr->leSon;
        else
            cellPtr = cellPtr->gSon;
    }

    //Once we found the cell containing the query point, the nearest neighbour has great chances to lie in this cell
    for(i=0; i<cellPtr->nbPoints; i++)
    {
        const CCVector3 *p = associatedCloud->getPoint(m_indexes[cellPtr->startingPointIndex+i]);
        sqrdist = CCVector3::vdistance2(p->u, queryPoint);
        if(sqrdist<maxDist)
        {
            maxDist = sqrdist;
            nearestPointIndex = m_indexes[cellPtr->startingPointIndex+i];
            found = true;
        }
    }

    //Go up in the tree to check that neighbours cells do not contain a nearer point than the one we found
    while(cellPtr != 0)
    {
        prevPtr = cellPtr;
        cellPtr = cellPtr->father;
        if(cellPtr != 0)
        {
            sqrdist = InsidePointToCellDistance(queryPoint, cellPtr);
            if(sqrdist>=0. && sqrdist*sqrdist<maxDist)
            {
                if(cellPtr->leSon == prevPtr)
                    brotherPtr = cellPtr->gSon;
                else
                    brotherPtr = cellPtr->leSon;
                a = checkNearerPointInSubTree(queryPoint, maxDist, brotherPtr);
                if(a >= 0)
                {
                    nearestPointIndex = a;
                    found = true;
                }
            }
            else
                cellPtr = 0;
        }
    }

    return found;
}


bool KDTree::findPointBelowDistance(
    const PointCoordinateType *queryPoint,
    PointCoordinateType maxDist)
{
    KdCell *cellPtr, *prevPtr, *brotherPtr;
    unsigned i;
    PointCoordinateType sqrdist;

    if(m_root == 0)
        return false;

    cellPtr = m_root;
    maxDist *= maxDist;
    //Go down the tree to find which cell contains the query point (at most log2(N) tests where N is the total number of points in the cloud)
    while(!(cellPtr->leSon == 0 && cellPtr->gSon == 0))
    {
        if(queryPoint[cellPtr->cuttingDim]<=cellPtr->cuttingCoordinate)
            cellPtr = cellPtr->leSon;
        else
            cellPtr = cellPtr->gSon;
    }

    //Once we found the cell containing the query point, there are great chance to find a point if it exists
    for(i=0; i<cellPtr->nbPoints; i++)
    {
		const CCVector3 *p = associatedCloud->getPoint(m_indexes[cellPtr->startingPointIndex+i]);
        sqrdist = CCVector3::vdistance2(p->u, queryPoint);
        if(sqrdist<maxDist)
            return true;
    }

    //Go up in the tree to check that neighbours cells do not contain a point
    while(cellPtr != 0)
    {
        prevPtr = cellPtr;
        cellPtr = cellPtr->father;
        if(cellPtr != 0)
        {
            sqrdist = InsidePointToCellDistance(queryPoint, cellPtr);
            if(sqrdist>=0. && sqrdist*sqrdist<maxDist)
            {
                if(cellPtr->leSon == prevPtr)
                    brotherPtr = cellPtr->gSon;
                else
                    brotherPtr = cellPtr->leSon;
                if(checkDistantPointInSubTree(queryPoint, maxDist, brotherPtr))
                    return true;
            }
            else
                cellPtr = 0;
        }
    }

    return false;
}

unsigned KDTree::findPointsLyingToDistance(const PointCoordinateType *queryPoint,
											PointCoordinateType distance,
											PointCoordinateType tolerance,
											std::vector<unsigned> &points)
{
    if(m_root == 0)
        return 0;

    distanceScanTree(queryPoint, distance, tolerance, m_root, points);

    return (unsigned)points.size();
}


void KDTree::updateInsideBoundingBox(KdCell* cell)
{
    if((cell->leSon!=0) && (cell->gSon!=0))
    {
        cell->inbbmax.x = std::max(cell->leSon->inbbmax.x, cell->gSon->inbbmax.x);
        cell->inbbmax.y = std::max(cell->leSon->inbbmax.y, cell->gSon->inbbmax.y);
        cell->inbbmax.z = std::max(cell->leSon->inbbmax.z, cell->gSon->inbbmax.z);
        cell->inbbmin.x = std::min(cell->leSon->inbbmin.x, cell->gSon->inbbmin.x);
        cell->inbbmin.y = std::min(cell->leSon->inbbmin.y, cell->gSon->inbbmin.y);
        cell->inbbmin.z = std::min(cell->leSon->inbbmin.z, cell->gSon->inbbmin.z);
    }
    else
    {
        CCVector3& p = m_list[cell->startingPointIndex].point;
        cell->inbbmin = cell->inbbmax = p;
        for(unsigned i=1; i<cell->nbPoints; i++)
        {
            p = m_list[i+cell->startingPointIndex].point;
            cell->inbbmax.x = std::max(cell->inbbmax.x, p.x);
            cell->inbbmax.y = std::max(cell->inbbmax.y, p.y);
            cell->inbbmax.z = std::max(cell->inbbmax.z, p.z);
            cell->inbbmin.x = std::min(cell->inbbmin.x, p.x);
            cell->inbbmin.y = std::min(cell->inbbmin.y, p.y);
            cell->inbbmin.z = std::min(cell->inbbmin.z, p.z);
        }
    }
}


void KDTree::updateOutsideBoundingBox(KdCell *cell)
{
    if(cell->father == 0)
    {
        cell->boundsMask = 0;
    }
    else
    {
        unsigned char bound = 1;
        cell->boundsMask = cell->father->boundsMask;
        cell->outbbmax = cell->father->outbbmax;
        cell->outbbmin = cell->father->outbbmin;
        const CCVector3& p = m_list[cell->startingPointIndex].point;
        //Check if this cell is its father leSon (if...) or gSon (else...)
        if(p.u[cell->father->cuttingDim] <= cell->father->cuttingCoordinate)
        {
            //Bounding box max point is linked to the bits [3..5] in the bounds mask
            bound = bound<<(3+cell->father->cuttingDim);
            cell->boundsMask = cell->boundsMask | bound;
            cell->outbbmax.u[cell->father->cuttingDim] = cell->father->cuttingCoordinate;
        }
        else
        {
            //Bounding box min point is linked to the bits[0..2] in the bounds mask
            bound = bound<<(cell->father->cuttingDim);
            cell->boundsMask = cell->boundsMask | bound;
            cell->outbbmin.u[cell->father->cuttingDim] = cell->father->cuttingCoordinate;
        }
    }
}


DistanceType KDTree::pointToCellSquareDistance(const PointCoordinateType *queryPoint, KdCell *cell)
{
    DistanceType dx, dy, dz;

    //Each d(x)(y)(z) represents the distance to the nearest bounding box plane (if the point is outside)
    if(cell->inbbmin.x<=queryPoint[0] && queryPoint[0]<=cell->inbbmax.x)
        dx = 0.;
    else
        dx = std::min(fabs(queryPoint[0]-cell->inbbmin.x), fabs(queryPoint[0]-cell->inbbmax.x));
    if(cell->inbbmin.y<=queryPoint[1] && queryPoint[1]<=cell->inbbmax.y)
        dy = 0.;
    else
        dy = std::min(fabs(queryPoint[1]-cell->inbbmin.y), fabs(queryPoint[1]-cell->inbbmax.y));
    if(cell->inbbmin.z<=queryPoint[2] && queryPoint[2]<=cell->inbbmax.z)
        dz = 0.;
    else
        dz = std::min(fabs(queryPoint[2]-cell->inbbmin.z), fabs(queryPoint[2]-cell->inbbmax.z));

    return (dx*dx)+(dy*dy)+(dz*dz);
}


void KDTree::pointToCellDistances(const PointCoordinateType *queryPoint, KdCell *cell, DistanceType& min, DistanceType &max)
{
    DistanceType dx, dy, dz;

    min = sqrt(pointToCellSquareDistance(queryPoint, cell));
    dx = std::max(fabs(queryPoint[0]-cell->inbbmin.x), fabs(queryPoint[0]-cell->inbbmax.x));
    dy = std::max(fabs(queryPoint[1]-cell->inbbmin.y), fabs(queryPoint[1]-cell->inbbmax.y));
    dz = std::max(fabs(queryPoint[2]-cell->inbbmin.z), fabs(queryPoint[2]-cell->inbbmax.z));
    max = sqrt((dx*dx)+(dy*dy)+(dz*dz));
}


DistanceType KDTree::InsidePointToCellDistance(const PointCoordinateType *queryPoint, KdCell *cell)
{
    DistanceType dx, dy, dz, max;

    dx = dy = dz = -1;

    if((cell->boundsMask&1) && (cell->boundsMask&8))
        dx = std::min(fabs(queryPoint[0]-cell->outbbmin.x), fabs(queryPoint[0]-cell->outbbmax.x));
    else if(cell->boundsMask&1)
        dx = fabs(queryPoint[0]-cell->outbbmin.x);
    else if(cell->boundsMask&8)
        dx = fabs(queryPoint[0]-cell->outbbmax.x);

    if((cell->boundsMask&2) && (cell->boundsMask&16))
        dy = std::min(fabs(queryPoint[1]-cell->outbbmin.y), fabs(queryPoint[1]-cell->outbbmax.y));
    else if(cell->boundsMask&2)
        dy = fabs(queryPoint[1]-cell->outbbmin.y);
    else if(cell->boundsMask&16)
        dy = fabs(queryPoint[1]-cell->outbbmax.y);

    if((cell->boundsMask&4) && (cell->boundsMask&32))
        dz = std::min(fabs(queryPoint[2]-cell->outbbmin.z), fabs(queryPoint[2]-cell->outbbmax.z));
    else if(cell->boundsMask&4)
        dz = fabs(queryPoint[2]-cell->outbbmin.z);
    else if(cell->boundsMask&32)
        dz = fabs(queryPoint[2]-cell->outbbmax.z);

    if(dx < 0. && dy < 0. && dz < 0.)
        return -1.;

    max = std::max(dx, std::max(dy, dz));
    if(dx < 0.)
        dx = max;
    if(dy < 0.)
        dy = max;
    if(dz < 0.)
        dz = max;

    return std::min(dx, std::min(dy, dz));
}


int KDTree::checkNearerPointInSubTree(const PointCoordinateType *queryPoint, DistanceType &maxSqrDist, KdCell *cell)
{
    if(pointToCellSquareDistance(queryPoint, cell)>=maxSqrDist)
        return -1;

    if(cell->leSon == 0 && cell->gSon == 0)
    {
        int a = -1;
        for(unsigned i=0; i<cell->nbPoints; i++)
        {
            const CCVector3 *p = associatedCloud->getPoint(m_indexes[cell->startingPointIndex+i]);
            DistanceType dist = CCVector3::vdistance2(p->u, queryPoint);
            if(dist<maxSqrDist)
            {
                a = m_indexes[cell->startingPointIndex+i];
                maxSqrDist = dist;
            }
        }

        return a;
    }

	int b = checkNearerPointInSubTree(queryPoint,  maxSqrDist, cell->gSon);
	if (b >= 0)
		return b;

	return checkNearerPointInSubTree(queryPoint,  maxSqrDist, cell->leSon);
}


bool KDTree::checkDistantPointInSubTree(const PointCoordinateType *queryPoint, DistanceType &maxSqrDist, KdCell *cell)
{
    if(pointToCellSquareDistance(queryPoint, cell)>=maxSqrDist)
        return false;

    if(cell->leSon == 0 && cell->gSon == 0)
    {
        for(unsigned i=0; i<cell->nbPoints; i++)
        {
            const CCVector3 *p = associatedCloud->getPoint(m_indexes[cell->startingPointIndex+i]);
            DistanceType dist = CCVector3::vdistance2(p->u, queryPoint);
            if(dist<maxSqrDist)
                return true;
        }
        return false;
    }

    if(checkDistantPointInSubTree(queryPoint,  maxSqrDist, cell->leSon))
        return true;
    if(checkDistantPointInSubTree(queryPoint,  maxSqrDist, cell->gSon))
        return true;

    return false;
}


void KDTree::distanceScanTree(
    const PointCoordinateType *queryPoint,
    DistanceType distance,
    DistanceType tolerance,
    KdCell *cell,
    std::vector<unsigned> &localArray)
{
    DistanceType min, max;

    pointToCellDistances(queryPoint, cell, min, max);

    if((min<=distance+tolerance) && (max>=distance-tolerance))
    {
        if((cell->leSon!=0) && (cell->gSon!=0))
        {
            //This case shall allways happen (the other case is for leaves that contain more than one point - bucket KDtree)
            if(cell->nbPoints == 1)
            {
                localArray.push_back(m_indexes[cell->startingPointIndex]);
            }
            else
            {
                for(unsigned i=0; i<cell->nbPoints; i++)
                {
                    const CCVector3 *p = associatedCloud->getPoint(m_indexes[i+cell->startingPointIndex]);
                    DistanceType dist = CCVector3::vdistance(queryPoint, p->u);
                    if((distance-tolerance<=dist) && (dist<=distance+tolerance))
                        localArray.push_back(m_indexes[cell->startingPointIndex+i]);
                }
            }
        }
        else
        {
            distanceScanTree(queryPoint, distance, tolerance, cell->leSon, localArray);
            distanceScanTree(queryPoint, distance, tolerance, cell->gSon, localArray);
        }
    }
}
