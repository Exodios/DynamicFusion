#include "WarpField.h"
#include "TsdfVolume.h"
#include "GpuMesh.h"
#include "GpuKdTree.h"
#include "ldp_basic_mat.h"
namespace dfusion
{
	WarpField::WarpField()
	{
		m_volume = nullptr;
		m_knnField = nullptr;
		m_nodeTree = nullptr;
		m_current_point_buffer_size = 0;
		memset(m_numNodes, 0, sizeof(m_numNodes));
		memset(m_lastNumNodes, 0, sizeof(m_lastNumNodes));
	}

	WarpField::~WarpField()
	{
	}

	void WarpField::init(TsdfVolume* vol, Param param)
	{
		m_param = param;
		m_volume = vol;
		m_rigidTransform = Tbx::Transfo::identity();
		m_nodesQuatTrans.create(MaxNodeNum*GraphLevelNum);
		m_nodesVW.create(MaxNodeNum*GraphLevelNum);
		for (int lv = 0; lv < GraphLevelNum; lv++)
			m_numNodes[lv] = 0;
		int3 res = m_volume->getResolution();
		float vsz = m_volume->getVoxelSize();

		// knn volume
		// malloc 3D texture
		if (m_knnField)
			cudaSafeCall(cudaFreeArray(m_knnField), "cudaFreeArray");
		cudaExtent ext = make_cudaExtent(res.x, res.y, res.z);
		cudaChannelFormatDesc desc = cudaCreateChannelDesc<KnnIdx>();
		cudaSafeCall(cudaMalloc3DArray(&m_knnField, &desc, ext), "cudaMalloc3D");

		if (m_nodeTree)
			delete m_nodeTree;
		m_nodeTree = new GpuKdTree();

		// nodes grid
		m_nodesGridSize = make_int3(std::ceil(res.x*vsz/m_param.warp_radius_search_epsilon),
			std::ceil(res.y*vsz/m_param.warp_radius_search_epsilon),
			std::ceil(res.z*vsz/m_param.warp_radius_search_epsilon));
	}

	void WarpField::updateWarpNodes(GpuMesh& src)
	{
		memcpy(m_lastNumNodes, m_numNodes, sizeof(m_numNodes));

		insertNewNodes(src);

		if (m_lastNumNodes[0] != m_numNodes[0])
		{
			updateAnnField();

			for (int lv = 1; lv < GraphLevelNum; lv++)
				updateGraph(lv);
		}
	}

	cudaSurfaceObject_t WarpField::bindKnnFieldSurface()
	{
		cudaSurfaceObject_t t;
		cudaResourceDesc    surfRes;
		memset(&surfRes, 0, sizeof(cudaResourceDesc));
		surfRes.resType = cudaResourceTypeArray;
		surfRes.res.array.array = m_knnField;
		cudaSafeCall(cudaCreateSurfaceObject(&t, &surfRes));
		return t;
	}

	void WarpField::unBindKnnFieldSurface(cudaSurfaceObject_t t)
	{
		cudaSafeCall(cudaDestroySurfaceObject(t));
	}

	cudaTextureObject_t WarpField::bindKnnFieldTexture()
	{
		cudaTextureObject_t t;
		cudaResourceDesc texRes;
		memset(&texRes, 0, sizeof(cudaResourceDesc));
		texRes.resType = cudaResourceTypeArray;
		texRes.res.array.array = m_knnField;
		cudaTextureDesc texDescr;
		memset(&texDescr, 0, sizeof(cudaTextureDesc));
		texDescr.normalizedCoords = 0;
		texDescr.filterMode = cudaFilterModePoint;
		texDescr.addressMode[0] = cudaAddressModeClamp;
		texDescr.addressMode[1] = cudaAddressModeClamp;
		texDescr.addressMode[2] = cudaAddressModeClamp;
		texDescr.readMode = cudaReadModeElementType;
		cudaSafeCall(cudaCreateTextureObject(&t, &texRes, &texDescr, NULL));
		return t;
	}

	void WarpField::unBindKnnFieldTexture(cudaTextureObject_t t)
	{
		cudaSafeCall(cudaDestroyTextureObject(t));
	}

}