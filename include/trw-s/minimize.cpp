#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "MRFEnergy.h"
#include "mex.h"
#include <cmath>

template <class T> int MRFEnergy<T>::Minimize_TRW_S(Options& options, REAL& lowerBound, REAL& energy)
{
	Node* i;
	Node* j;
	MRFEdge* e;
	REAL vMin;
	int iter;
	REAL lowerBoundPrev;

	if (!m_isEnergyConstructionCompleted)
	{
		CompleteGraphConstruction();
	}

	SetMonotonicTrees();

	Vector* Di = (Vector*) m_buf;
	void* buf = (void*) (m_buf + m_vectorMaxSizeInBytes);

	iter = 0;

	// main loop
	for (iter=1; ; iter++)
	{
		////////////////////////////////////////////////
		//                forward pass                //
		////////////////////////////////////////////////
		for (i=m_nodeFirst; i; i=i->m_next)
		{
			Di->Copy(m_Kglobal, i->m_K, &i->m_D);
			for (e=i->m_firstForward; e; e=e->m_nextForward)
			{
				Di->Add(m_Kglobal, i->m_K, e->m_message.GetMessagePtr());
			}
			for (e=i->m_firstBackward; e; e=e->m_nextBackward)
			{
				Di->Add(m_Kglobal, i->m_K, e->m_message.GetMessagePtr());
			}

			// normalize Di, update lower bound
			// vMin = Di->ComputeAndSubtractMin(m_Kglobal, i->m_K); // do not compute lower bound
			// lowerBound += vMin;                                  // during the forward pass

			// pass messages from i to nodes with higher m_ordering
			for (e=i->m_firstForward; e; e=e->m_nextForward)
			{
				assert(e->m_tail == i);
				j = e->m_head;

				vMin = e->m_message.UpdateMessage(m_Kglobal, i->m_K, j->m_K, Di, e->m_gammaForward, 0, buf);

				// lowerBound += vMin; // do not compute lower bound during the forward pass
			}
		}

		////////////////////////////////////////////////
		//               backward pass                //
		////////////////////////////////////////////////
		lowerBound = 0;

		for (i=m_nodeLast; i; i=i->m_prev)
		{
			Di->Copy(m_Kglobal, i->m_K, &i->m_D);
			for (e=i->m_firstBackward; e; e=e->m_nextBackward)
			{
				Di->Add(m_Kglobal, i->m_K, e->m_message.GetMessagePtr());
			}
			for (e=i->m_firstForward; e; e=e->m_nextForward)
			{
				Di->Add(m_Kglobal, i->m_K, e->m_message.GetMessagePtr());
			}

			// normalize Di, update lower bound
			vMin = Di->ComputeAndSubtractMin(m_Kglobal, i->m_K);
			lowerBound += vMin;

			// pass messages from i to nodes with smaller m_ordering
			for (e=i->m_firstBackward; e; e=e->m_nextBackward)
			{
				assert(e->m_head == i);
				j = e->m_tail;

				vMin = e->m_message.UpdateMessage(m_Kglobal, i->m_K, j->m_K, Di, e->m_gammaBackward, 1, buf);

				lowerBound += vMin;
			}
		}

		////////////////////////////////////////////////
		//          check stopping criterion          //
		////////////////////////////////////////////////
		bool finishFlag = false;
		if (iter >= options.m_iterMax)
			finishFlag = true;

		energy = ComputeSolutionAndEnergy();
		REAL rel_gap = (energy - lowerBound)/energy;
		
		if (options.m_printMinIter)
		{
			mexPrintf("iter: %d ", iter);

			if (std::isinf(energy))
				mexPrintf("lower bound: %g, inconsistent solution. \n", lowerBound);
			else
				mexPrintf("energy: %g lower bound: %g rel_gap: %g \n", energy, lowerBound, rel_gap);

			mexEvalString("drawnow");
		}

		if (rel_gap < options.m_relgapMax)
			finishFlag = true;

		// if finishFlag==true terminate
		if (finishFlag)
			break;
	}

	return iter;
}

template <class T> int MRFEnergy<T>::Minimize_BP(Options& options, REAL& energy)
{
	Node* i;
	Node* j;
	MRFEdge* e;
	REAL vMin;
	int iter;

	if (!m_isEnergyConstructionCompleted)
	{
		CompleteGraphConstruction();
	}

	Vector* Di = (Vector*) m_buf;
	void* buf = (void*) (m_buf + m_vectorMaxSizeInBytes);

	iter = 0;

	// main loop
	for (iter=1; ; iter++)
	{
		////////////////////////////////////////////////
		//                forward pass                //
		////////////////////////////////////////////////
		for (i=m_nodeFirst; i; i=i->m_next)
		{
			Di->Copy(m_Kglobal, i->m_K, &i->m_D);
			for (e=i->m_firstForward; e; e=e->m_nextForward)
			{
				Di->Add(m_Kglobal, i->m_K, e->m_message.GetMessagePtr());
			}
			for (e=i->m_firstBackward; e; e=e->m_nextBackward)
			{
				Di->Add(m_Kglobal, i->m_K, e->m_message.GetMessagePtr());
			}

			// pass messages from i to nodes with higher m_ordering
			for (e=i->m_firstForward; e; e=e->m_nextForward)
			{
				assert(i == e->m_tail);
				j = e->m_head;

				const REAL gamma = 1;

				e->m_message.UpdateMessage(m_Kglobal, i->m_K, j->m_K, Di, gamma, 0, buf);
			}
		}

		////////////////////////////////////////////////
		//               backward pass                //
		////////////////////////////////////////////////

		for (i=m_nodeLast; i; i=i->m_prev)
		{
			Di->Copy(m_Kglobal, i->m_K, &i->m_D);
			for (e=i->m_firstBackward; e; e=e->m_nextBackward)
			{
				Di->Add(m_Kglobal, i->m_K, e->m_message.GetMessagePtr());
			}
			for (e=i->m_firstForward; e; e=e->m_nextForward)
			{
				Di->Add(m_Kglobal, i->m_K, e->m_message.GetMessagePtr());
			}

			// pass messages from i to nodes with smaller m_ordering
			for (e=i->m_firstBackward; e; e=e->m_nextBackward)
			{
				assert(i == e->m_head);
				j = e->m_tail;

				const REAL gamma = 1;

				vMin = e->m_message.UpdateMessage(m_Kglobal, i->m_K, j->m_K, Di, gamma, 1, buf);
			}
		}

		////////////////////////////////////////////////
		//          check stopping criterion          //
		////////////////////////////////////////////////
		bool finishFlag = false;
		if (iter >= options.m_iterMax)
		{
			finishFlag = true;
		}

		// if finishFlag==true terminate
		if (finishFlag)
		{
			break;
		}
	}

	return iter;
}

template <class T> typename T::REAL MRFEnergy<T>::ComputeSolutionAndEnergy()
{
	Node* i;
	Node* j;
	MRFEdge* e;
	REAL E = 0;

	Vector* DiBackward = (Vector*) m_buf; // cost of backward edges plus Di at the node
	Vector* Di = (Vector*) (m_buf + m_vectorMaxSizeInBytes); // all edges plus Di at the node

	for (i=m_nodeFirst; i; i=i->m_next)
	{
		// Set Ebackward[ki] to be the sum of V(ki,j->m_solution) for backward edges (i,j).
		// Set Di[ki] to be the value of the energy corresponding to
		// part of the graph considered so far, assuming that nodes u
		// in this subgraph are fixed to u->m_solution

		DiBackward->Copy(m_Kglobal, i->m_K, &i->m_D);
		for (e=i->m_firstBackward; e; e=e->m_nextBackward)
		{
			assert(i == e->m_head);
			j = e->m_tail;

			e->m_message.AddColumn(m_Kglobal, j->m_K, i->m_K, j->m_solution, DiBackward, 0);
		}

		// add forward edges
		Di->Copy(m_Kglobal, i->m_K, DiBackward);

		for (e=i->m_firstForward; e; e=e->m_nextForward)
		{
			Di->Add(m_Kglobal, i->m_K, e->m_message.GetMessagePtr());
		}

		Di->ComputeMin(m_Kglobal, i->m_K, i->m_solution);

		// update energy
		E += DiBackward->GetValue(m_Kglobal, i->m_K, i->m_solution);
	}

	return E;
}

#include "instances.inc"
