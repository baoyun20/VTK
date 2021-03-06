/*=========================================================================

  Program:   Visualization Toolkit

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkglVBOHelper.h"

#include "vtkCellArray.h"
#include "vtkPoints.h"
#include "vtkPolygon.h"
#include "vtkPolyData.h"
#include "vtkShaderProgram.h"


// we only instantiate some cases to avoid template explosion
#define vtkFloatDoubleTemplateMacro(call)                                              \
  vtkTemplateMacroCase(VTK_DOUBLE, double, call);                           \
  vtkTemplateMacroCase(VTK_FLOAT, float, call);


namespace vtkgl {

// internal function called by CreateVBO
template<typename T, typename T2, typename T3>
VBOLayout TemplatedCreateVBO3(T* points, T2* normals, vtkIdType numPts,
                    T3* tcoords, int textureComponents,
                    unsigned char *colors, int colorComponents,
                    BufferObject &vertexBuffer, unsigned int *cellPointMap,
                    unsigned int *pointCellMap,
                    bool cellScalars, bool cellNormals)
{
  VBOLayout layout;
  // Figure out how big each block will be, currently 6 or 7 floats.
  int blockSize = 3;
  layout.VertexOffset = 0;
  layout.NormalOffset = 0;
  layout.TCoordOffset = 0;
  layout.TCoordComponents = 0;
  layout.ColorComponents = 0;
  layout.ColorOffset = 0;
  if (normals)
    {
    layout.NormalOffset = sizeof(float) * blockSize;
    blockSize += 3;
    }
  if (tcoords)
    {
    layout.TCoordOffset = sizeof(float) * blockSize;
    layout.TCoordComponents = textureComponents;
    blockSize += textureComponents;
    }
  if (colors)
    {
    layout.ColorComponents = colorComponents;
    layout.ColorOffset = sizeof(float) * blockSize;
    ++blockSize;
    }
  layout.Stride = sizeof(float) * blockSize;

  // Create a buffer, and copy the data over.
  std::vector<float> packedVBO;
  packedVBO.resize(blockSize * numPts);
  std::vector<float>::iterator it = packedVBO.begin();

  T *pointPtr;
  T2 *normalPtr;
  T3 *tcoordPtr;
  unsigned char *colorPtr;


  // TODO: optimize this somehow, lots of if statements in here
  for (vtkIdType i = 0; i < numPts; ++i)
    {
    if (cellPointMap && cellPointMap[i] > 0)
      {
      pointPtr = points + (cellPointMap[i]-1)*3;
      normalPtr = normals + (cellPointMap[i]-1)*3;
      tcoordPtr = tcoords + (cellPointMap[i]-1)*textureComponents;
      colorPtr = colors + (cellPointMap[i]-1)*colorComponents;
      }
    else
      {
      pointPtr = points + i*3;
      normalPtr = normals + i*3;
      tcoordPtr = tcoords + i*textureComponents;
      colorPtr = colors + i*colorComponents;
      }
    // Vertices
    *(it++) = *(pointPtr++);
    *(it++) = *(pointPtr++);
    *(it++) = *(pointPtr++);
    if (normals)
      {
      if (cellNormals)
        {
        normalPtr = normals + pointCellMap[i]*3;
        }
      *(it++) = *(normalPtr++);
      *(it++) = *(normalPtr++);
      *(it++) = *(normalPtr++);
      }
    if (tcoords)
      {
      for (int j = 0; j < textureComponents; ++j)
        {
        *(it++) = *(tcoordPtr++);
        }
      }
    if (colors)
      {
      if (cellScalars)
        {
        colorPtr = colors + pointCellMap[i]*colorComponents;
        }
      if (colorComponents == 4)
        {
        *(it++) = *reinterpret_cast<float *>(colorPtr);
        }
      else
        {
        unsigned char c[4];
        c[0] = *(colorPtr++);
        c[1] = *(colorPtr++);
        c[2] = *(colorPtr);
        c[3] =  255;
        *(it++) = *reinterpret_cast<float *>(c);
        }
      }
    }
  vertexBuffer.Upload(packedVBO, vtkgl::BufferObject::ArrayBuffer);
  layout.VertexCount = numPts;
  return layout;
}

//----------------------------------------------------------------------------
template<typename T, typename T2>
VBOLayout TemplatedCreateVBO2(T* points, T2 *normals, vtkIdType numPts,
                    vtkDataArray *tcoords,
                    unsigned char *colors, int colorComponents,
                    BufferObject &vertexBuffer, unsigned int *cellPointMap,
                    unsigned int *pointCellMap,
                    bool cellScalars, bool cellNormals)
{
  if (tcoords)
    {
    switch(tcoords->GetDataType())
      {
      vtkFloatDoubleTemplateMacro(
        return
          TemplatedCreateVBO3(points, normals,
                    numPts,
                    static_cast<VTK_TT*>(tcoords->GetVoidPointer(0)),
                    tcoords->GetNumberOfComponents(),
                    colors, colorComponents,
                    vertexBuffer, cellPointMap, pointCellMap,
                    cellScalars, cellNormals));
      }
    }
  else
    {
    return
      TemplatedCreateVBO3(points, normals,
                          numPts, (float *)NULL, 0,
                          colors, colorComponents,
                          vertexBuffer, cellPointMap, pointCellMap,
                          cellScalars, cellNormals);
    }
  return VBOLayout();
}


//----------------------------------------------------------------------------
template<typename T>
VBOLayout TemplatedCreateVBO(T* points, vtkDataArray *normals, vtkIdType numPts,
                    vtkDataArray *tcoords,
                    unsigned char *colors, int colorComponents,
                    BufferObject &vertexBuffer, unsigned int *cellPointMap,
                    unsigned int *pointCellMap,
                    bool cellScalars, bool cellNormals)
{
  if (normals)
    {
    switch(normals->GetDataType())
      {
      vtkFloatDoubleTemplateMacro(
        return
          TemplatedCreateVBO2(points,
                    static_cast<VTK_TT*>(normals->GetVoidPointer(0)),
                    numPts, tcoords, colors, colorComponents,
                    vertexBuffer, cellPointMap, pointCellMap,
                    cellScalars, cellNormals));
      }
    }
  else
    {
    return
      TemplatedCreateVBO2(points,
                          (float *)NULL,
                          numPts, tcoords, colors, colorComponents,
                          vertexBuffer, cellPointMap, pointCellMap,
                          cellScalars, cellNormals);
    }
  return VBOLayout();
}


// Take the points, and pack them into the VBO object supplied. This currently
// takes whatever the input type might be and packs them into a VBO using
// floats for the vertices and normals, and unsigned char for the colors (if
// the array is non-null).
VBOLayout CreateVBO(vtkPoints *points, unsigned int numPts,
                    vtkDataArray *normals,
                    vtkDataArray *tcoords,
                    unsigned char *colors, int colorComponents,
                    BufferObject &vertexBuffer, unsigned int *cellPointMap, unsigned int *pointCellMap,
                    bool cellScalars, bool cellNormals)
{
  // fast path
  if (!tcoords && !normals && !colors && points->GetDataType() == VTK_FLOAT)
    {
    VBOLayout layout;
    int blockSize = 3;
    layout.VertexOffset = 0;
    layout.NormalOffset = 0;
    layout.TCoordOffset = 0;
    layout.TCoordComponents = 0;
    layout.ColorComponents = 0;
    layout.ColorOffset = 0;
    layout.Stride = sizeof(float) * blockSize;
    vertexBuffer.Upload((float *)(points->GetVoidPointer(0)), numPts*3, vtkgl::BufferObject::ArrayBuffer);
    layout.VertexCount = numPts;
    return layout;
    }

  //slower path
  switch(points->GetDataType())
    {
    vtkTemplateMacro(
      return
        TemplatedCreateVBO(static_cast<VTK_TT*>(points->GetVoidPointer(0)),
                  normals, numPts, tcoords, colors, colorComponents,
                  vertexBuffer, cellPointMap, pointCellMap,
                  cellScalars, cellNormals));
    }
  return VBOLayout();
}


// Process the string, and return a version with replacements.
std::string replace(std::string source, const std::string &search,
                    const std::string replace, bool all)
{
  std::string::size_type pos = 0;
  while ((pos = source.find(search, 0)) != std::string::npos)
    {
    source.replace(pos, search.length(), replace);
    if (!all)
      {
      return source;
      }
    pos += search.length();
    }
  return source;
}

// Process the string, and return a version with replacements.
bool substitute(std::string &source, const std::string &search,
             const std::string replace, bool all)
{
  std::string::size_type pos = 0;
  bool replaced = false;
  while ((pos = source.find(search, 0)) != std::string::npos)
    {
    source.replace(pos, search.length(), replace);
    if (!all)
      {
      return true;
      }
    pos += search.length();
    replaced = true;
    }
  return replaced;
}

// used to create an IBO for triangle primatives
size_t CreateTriangleIndexBuffer(vtkCellArray *cells, BufferObject &indexBuffer,
                                 vtkPoints *points, std::vector<unsigned int> &cellPointMap)
{
  std::vector<unsigned int> indexArray;
  vtkIdType* indices(NULL);
  vtkIdType npts(0);
  indexArray.reserve(cells->GetNumberOfCells() * 3);

  // the folowing are only used if we have to triangulate a polygon
  // otherwise they just sit at NULL
  vtkPolygon *polygon = NULL;
  vtkIdList *tris = NULL;
  vtkPoints *triPoints = NULL;

  for (cells->InitTraversal(); cells->GetNextCell(npts, indices); )
    {
    // ignore degenerate triangles
    if (npts < 3)
      {
      continue;
      }

    // triangulate needed
    if (npts > 3)
      {
      // special case for quads, penta, hex which are common
      if (npts == 4)
        {
        indexArray.push_back(static_cast<unsigned int>(indices[0]));
        indexArray.push_back(static_cast<unsigned int>(indices[1]));
        indexArray.push_back(static_cast<unsigned int>(indices[2]));
        indexArray.push_back(static_cast<unsigned int>(indices[0]));
        indexArray.push_back(static_cast<unsigned int>(indices[2]));
        indexArray.push_back(static_cast<unsigned int>(indices[3]));
        }
      else if (npts == 5)
        {
        indexArray.push_back(static_cast<unsigned int>(indices[0]));
        indexArray.push_back(static_cast<unsigned int>(indices[1]));
        indexArray.push_back(static_cast<unsigned int>(indices[2]));
        indexArray.push_back(static_cast<unsigned int>(indices[0]));
        indexArray.push_back(static_cast<unsigned int>(indices[2]));
        indexArray.push_back(static_cast<unsigned int>(indices[3]));
        indexArray.push_back(static_cast<unsigned int>(indices[0]));
        indexArray.push_back(static_cast<unsigned int>(indices[3]));
        indexArray.push_back(static_cast<unsigned int>(indices[4]));
        }
      else if (npts == 6)
        {
        indexArray.push_back(static_cast<unsigned int>(indices[0]));
        indexArray.push_back(static_cast<unsigned int>(indices[1]));
        indexArray.push_back(static_cast<unsigned int>(indices[2]));
        indexArray.push_back(static_cast<unsigned int>(indices[0]));
        indexArray.push_back(static_cast<unsigned int>(indices[2]));
        indexArray.push_back(static_cast<unsigned int>(indices[3]));
        indexArray.push_back(static_cast<unsigned int>(indices[0]));
        indexArray.push_back(static_cast<unsigned int>(indices[3]));
        indexArray.push_back(static_cast<unsigned int>(indices[5]));
        indexArray.push_back(static_cast<unsigned int>(indices[3]));
        indexArray.push_back(static_cast<unsigned int>(indices[4]));
        indexArray.push_back(static_cast<unsigned int>(indices[5]));
        }
      else // 7 sided polygon or higher, do a full smart triangulation
        {
        if (!polygon)
          {
          polygon = vtkPolygon::New();
          tris = vtkIdList::New();
          triPoints = vtkPoints::New();
          }

        vtkIdType *triIndices = new vtkIdType[npts];
        triPoints->SetNumberOfPoints(npts);
        for (int i = 0; i < npts; ++i)
          {
          int idx = indices[i];
          if (cellPointMap.size() > 0 && cellPointMap[indices[i]] > 0)
            {
            idx = cellPointMap[indices[i]]-1;
            }
          triPoints->SetPoint(i,points->GetPoint(idx));
          triIndices[i] = i;
          }
        polygon->Initialize(npts, triIndices, triPoints);
        polygon->Triangulate(tris);
        for (int j = 0; j < tris->GetNumberOfIds(); ++j)
          {
          indexArray.push_back(static_cast<unsigned int>(indices[tris->GetId(j)]));
          }
        delete [] triIndices;
        }
      }
    else
      {
      indexArray.push_back(static_cast<unsigned int>(*(indices++)));
      indexArray.push_back(static_cast<unsigned int>(*(indices++)));
      indexArray.push_back(static_cast<unsigned int>(*(indices++)));
      }
    }
  if (polygon)
    {
    polygon->Delete();
    tris->Delete();
    triPoints->Delete();
    }
  indexBuffer.Upload(indexArray, vtkgl::BufferObject::ElementArrayBuffer);
  return indexArray.size();
}

// used to create an IBO for point primatives
size_t CreatePointIndexBuffer(vtkCellArray *cells, BufferObject &indexBuffer)
{
  std::vector<unsigned int> indexArray;
  vtkIdType* indices(NULL);
  vtkIdType npts(0);
  indexArray.reserve(cells->GetNumberOfConnectivityEntries());

  for (cells->InitTraversal(); cells->GetNextCell(npts, indices); )
    {
    for (int i = 0; i < npts; ++i)
      {
      indexArray.push_back(static_cast<unsigned int>(*(indices++)));
      }
    }
  indexBuffer.Upload(indexArray, vtkgl::BufferObject::ElementArrayBuffer);
  return indexArray.size();
}

// used to create an IBO for primatives as lines.  This method treats each line segment
// as independent.  So for a triangle mesh you would get 6 verts per triangle
// 3 edges * 2 verts each.  With a line loop you only get 3 verts so half the storage.
// but... line loops are slower than line segments.
size_t CreateTriangleLineIndexBuffer(vtkCellArray *cells, BufferObject &indexBuffer)
{
  std::vector<unsigned int> indexArray;
  vtkIdType* indices(NULL);
  vtkIdType npts(0);
  indexArray.reserve(cells->GetNumberOfConnectivityEntries()*2);

  for (cells->InitTraversal(); cells->GetNextCell(npts, indices); )
    {
    for (int i = 0; i < npts; ++i)
      {
      indexArray.push_back(static_cast<unsigned int>(indices[i]));
      indexArray.push_back(static_cast<unsigned int>(indices[i < npts-1 ? i+1 : 0]));
      }
    }
  indexBuffer.Upload(indexArray, vtkgl::BufferObject::ElementArrayBuffer);
  return indexArray.size();
}

// used to create an IBO for stripped primatives such as lines and triangle strips
size_t CreateMultiIndexBuffer(vtkCellArray *cells, BufferObject &indexBuffer,
                              std::vector<GLintptr> &memoryOffsetArray,
                              std::vector<unsigned int> &elementCountArray,
                              bool wireframeTriStrips)
{
  vtkIdType      *pts = 0;
  vtkIdType      npts = 0;
  std::vector<unsigned int> indexArray;
  memoryOffsetArray.clear();
  elementCountArray.clear();
  unsigned int count = 0;
  indexArray.reserve(cells->GetData()->GetSize());
  for (cells->InitTraversal(); cells->GetNextCell(npts,pts); )
    {
    memoryOffsetArray.push_back(count*sizeof(unsigned int));
    for (int j = 0; j < npts; ++j)
      {
      indexArray.push_back(static_cast<unsigned int>(pts[j]));
      count++;
      }
    if (wireframeTriStrips)
      {
      for (int j = (npts-1)/2; j >= 0; j--)
        {
        indexArray.push_back(static_cast<unsigned int>(pts[j*2]));
        count++;
        }
      for (int j = 1; j < (npts/2)*2; j += 2)
        {
        indexArray.push_back(static_cast<unsigned int>(pts[j]));
        count++;
        }
      npts *= 2;
      }
    elementCountArray.push_back(npts);
    }
  indexBuffer.Upload(indexArray, vtkgl::BufferObject::ElementArrayBuffer);
  return indexArray.size();
}

// used to create an IBO for polys in wireframe with edge flags
size_t CreateEdgeFlagIndexBuffer(vtkCellArray *cells, BufferObject &indexBuffer,
                                 vtkDataArray *ef)
{
  vtkIdType      *pts = 0;
  vtkIdType      npts = 0;
  std::vector<unsigned int> indexArray;
  unsigned char *ucef = NULL;
  ucef = vtkUnsignedCharArray::SafeDownCast(ef)->GetPointer(0);
  indexArray.reserve(cells->GetData()->GetSize()*2);
  for (cells->InitTraversal(); cells->GetNextCell(npts,pts); )
    {
    for (int j = 0; j < npts; ++j)
      {
      if (ucef[pts[j]] && npts > 1) // draw this edge and poly is not degenerate
        {
        // determine the ending vertex
        vtkIdType nextVert = (j == npts-1) ? pts[0] : pts[j+1];
        indexArray.push_back(static_cast<unsigned int>(pts[j]));
        indexArray.push_back(static_cast<unsigned int>(nextVert));
        }
      }
    }
  indexBuffer.Upload(indexArray, vtkgl::BufferObject::ElementArrayBuffer);
  return indexArray.size();
}

// used to create an IBO for stripped primatives such as lines and triangle strips
void CreateCellSupportArrays(vtkPolyData *poly, vtkCellArray *prims[4],
                             std::vector<unsigned int> &cellPointMap,
                             std::vector<unsigned int> &pointCellMap)
{
  vtkCellArray *newPrims[4];

  // need an array to track what points have already been used
  cellPointMap.resize(prims[0]->GetSize() +
                       prims[1]->GetSize() +
                       prims[2]->GetSize() +
                       prims[3]->GetSize(), 0);
  // need an array to track what cells the points are part of
  pointCellMap.resize(prims[0]->GetSize() +
                     prims[1]->GetSize() +
                     prims[2]->GetSize() +
                     prims[3]->GetSize(), 0);
  vtkIdType* indices(NULL);
  vtkIdType npts(0);
  unsigned int nextId = poly->GetPoints()->GetNumberOfPoints();
  // make sure we have at least Num Points entries
  if (cellPointMap.size() < nextId)
    {
      cellPointMap.resize(nextId);
      pointCellMap.resize(nextId);
    }

  unsigned int cellCount = 0;
  for (int primType = 0; primType < 4; primType++)
    {
    newPrims[primType] = vtkCellArray::New();
    for (prims[primType]->InitTraversal(); prims[primType]->GetNextCell(npts, indices); )
      {
      newPrims[primType]->InsertNextCell(npts);

      for (int i=0; i < npts; ++i)
        {
        // point not used yet?
        if (cellPointMap[indices[i]] == 0)
          {
          cellPointMap[indices[i]] =  indices[i] + 1;
          newPrims[primType]->InsertCellPoint(indices[i]);
          pointCellMap[indices[i]] = cellCount;
          }
        // point used, need new point
        else
          {
          // might be beyond the current allocation
          if (nextId >= cellPointMap.size())
            {
            cellPointMap.resize(nextId*1.5);
            pointCellMap.resize(nextId*1.5);
            }
          cellPointMap[nextId] = indices[i] + 1;
          newPrims[primType]->InsertCellPoint(nextId);
          pointCellMap[nextId] = cellCount;
          nextId++;
          }
        }
        cellCount++;
      } // for cell

    prims[primType] = newPrims[primType];
    } // for primType

  cellPointMap.resize(nextId);
  pointCellMap.resize(nextId);
}



void CellBO::ReleaseGraphicsResources(vtkWindow * vtkNotUsed(win))
{
  if (this->Program)
    {
    // Let ShaderCache release the graphics resources as it is
    // responsible for creation and deletion.
    this->Program = 0;
    }
  this->ibo.ReleaseGraphicsResources();
  this->vao.ReleaseGraphicsResources();
  this->offsetArray.clear();
  this->elementsArray.clear();
}

}
