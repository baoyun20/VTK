vtk_module(vtkCommonExecutionModel
  DEPENDS
    vtkCommonDataModel
  COMPILE_DEPENDS
    vtkCommonMisc
  TEST_DEPENDS
    vtkTestingCore
    vtkIOCore
    vtkIOGeometry
    vtkFiltersExtraction
  )
