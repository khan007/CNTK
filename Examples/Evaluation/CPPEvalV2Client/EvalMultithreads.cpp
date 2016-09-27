#include <functional>
#include <thread>
#include <iostream>
#include "CNTKLibrary.h"

using namespace CNTK;

inline FunctionPtr FullyConnectedDNNLayerWithSharedParameters(Variable input,
                                                       const Parameter& timesParam,
                                                       const Parameter& plusParam,
                                                       const std::function<FunctionPtr(const FunctionPtr&)>& nonLinearity)
{
    assert(input.Shape().Rank() == 1);

    // Todo: assume that timesParam has matched outputDim and inputDim 
    auto timesFunction = Times(timesParam, input);

    // Todo: assume that timesParam has matched outputDim 
    auto plusFunction = Plus(plusParam, timesFunction);

    return nonLinearity(plusFunction);
}

inline FunctionPtr FullyConnectedFeedForwardClassifierNetWithSharedParameters(Variable input,
                                                                       size_t numHiddenLayers,
                                                                       const Parameter& inputTimesParam,
                                                                       const Parameter& inputPlusParam,
                                                                       const Parameter hiddenLayerTimesParam[],
                                                                       const Parameter hiddenLayerPlusParam[],
                                                                       const Parameter& outputTimesParam,
                                                                       const std::function<FunctionPtr(const FunctionPtr&)>& nonLinearity)
{
    assert(numHiddenLayers >= 1);
    auto classifierRoot = FullyConnectedDNNLayerWithSharedParameters(input, inputTimesParam, inputPlusParam, nonLinearity);

    for (size_t i = 1; i < numHiddenLayers; ++i)
        classifierRoot = FullyConnectedDNNLayerWithSharedParameters(classifierRoot, hiddenLayerTimesParam[i - 1], hiddenLayerPlusParam[i - 1], nonLinearity);

    // Todo: assume that outputTimesParam has matched output dim and hiddenLayerDim
    classifierRoot = Times(outputTimesParam, classifierRoot);
    return classifierRoot;
}

void EvaluationNewNetworkWithSharedParameters(size_t inputDim,
                                              size_t numOutputClasses,
                                              size_t numHiddenLayers,
                                              const Parameter& inputTimesParam,
                                              const Parameter& inputPlusParam,
                                              const Parameter hiddenLayerTimesParam[],
                                              const Parameter hiddenLayerPlusParam[],
                                              const Parameter& outputTimesParam,
                                              const DeviceDescriptor& computeDevice)
{
    using namespace std::placeholders;

    // Create network using shared parameters
    auto inputVar = InputVariable({inputDim}, DataType::Float, L"Features");
    auto classifierOutputFunction = FullyConnectedFeedForwardClassifierNetWithSharedParameters(inputVar,
                                                                                               numHiddenLayers,
                                                                                               inputTimesParam,
                                                                                               inputPlusParam,
                                                                                               hiddenLayerTimesParam,
                                                                                               hiddenLayerPlusParam,
                                                                                               outputTimesParam,
                                                                                               std::bind(Sigmoid, _1, L""));

    auto labelsVar = InputVariable({numOutputClasses}, DataType::Float, L"Labels");
    auto trainingLossFunction = CNTK::CrossEntropyWithSoftmax(classifierOutputFunction, labelsVar, L"LossFunction");
    auto predictionFunction = CNTK::ClassificationError(classifierOutputFunction, labelsVar, L"ClassificationError");

    auto ffNet = CNTK::Combine({trainingLossFunction, predictionFunction, classifierOutputFunction}, L"ClassifierModel");

    if (ffNet->Parameters().size() != ((numHiddenLayers * 2) + 1))
        throw std::runtime_error("EvaluationNewNetworkWithSharedParameters: Function does not have expected Parameter count");

    if (ffNet->Arguments().size() != 2)
        throw std::runtime_error("EvaluationNewNetworkWithSharedParameters: Function does not have expected Argument count");

    if (ffNet->Outputs().size() != 3)
        throw std::runtime_error("EvaluationNewNetworkWithSharedParameters: Function does not have expected Output count");

    // Evaluate the network in several runs 
    size_t iterationCount = 4;
    unsigned int randSeed = 2;
    srand(randSeed);
    size_t numSamples = 3;
    for (size_t t = 0; t < iterationCount; ++t)
    {
        std::vector<float> inputData(inputDim * numSamples);
        for (size_t i = 0; i < inputData.size(); ++i)
            inputData[i] = ((float)rand()) / RAND_MAX;

        NDShape inputShape = {inputDim, 1, numSamples};
        ValuePtr inputValue = MakeSharedObject<Value>(MakeSharedObject<NDArrayView>(inputShape, inputData.data(), inputData.size(), DeviceDescriptor::CPUDevice(), true));

        std::vector<float> labelData(numOutputClasses * numSamples, 0);
        for (size_t i = 0; i < numSamples; ++i)
            labelData[(i*numOutputClasses) + (rand() % numOutputClasses)] = 1;

        NDShape labelShape = {numOutputClasses, 1, numSamples};
        ValuePtr labelValue = MakeSharedObject<Value>(MakeSharedObject<NDArrayView>(labelShape, labelData.data(), labelData.size(), DeviceDescriptor::CPUDevice(), true));

        ValuePtr outputValue, predictionErrorValue;
        std::unordered_map<Variable, ValuePtr> outputs = {{classifierOutputFunction->Output(), outputValue}, {predictionFunction->Output(), predictionErrorValue}};
        ffNet->Forward({{inputVar, inputValue}, {labelsVar, labelValue}}, outputs, computeDevice);
    }
}

void EvalMultiThreadsWithNewNetwork(const DeviceDescriptor& device, const int threadCount)
{
    const size_t inputDim = 937;
    const size_t numOutputClasses = 9304;
    const size_t numHiddenLayers = 6;
    const size_t hiddenLayersDim = 2048;

    // Define model parameters that should be shared among evaluation requests against the same model
    auto inputTimesParam = Parameter(NDArrayView::RandomUniform<float>({hiddenLayersDim, inputDim}, -0.5, 0.5, 1, device));
    auto inputPlusParam = Parameter({hiddenLayersDim}, 0.0f, device);
    Parameter hiddenLayerTimesParam[numHiddenLayers - 1] = {
        Parameter(NDArrayView::RandomUniform<float>({hiddenLayersDim, hiddenLayersDim}, -0.5, 0.5, 1, device)),
        Parameter(NDArrayView::RandomUniform<float>({hiddenLayersDim, hiddenLayersDim}, -0.5, 0.5, 1, device)),
        Parameter(NDArrayView::RandomUniform<float>({hiddenLayersDim, hiddenLayersDim}, -0.5, 0.5, 1, device)),
        Parameter(NDArrayView::RandomUniform<float>({hiddenLayersDim, hiddenLayersDim}, -0.5, 0.5, 1, device)),
        Parameter(NDArrayView::RandomUniform<float>({hiddenLayersDim, hiddenLayersDim}, -0.5, 0.5, 1, device))
    };
    Parameter hiddenLayerPlusParam[numHiddenLayers - 1] = {
        Parameter({hiddenLayersDim}, 0.0f, device),
        Parameter({hiddenLayersDim}, 0.0f, device),
        Parameter({hiddenLayersDim}, 0.0f, device),
        Parameter({hiddenLayersDim}, 0.0f, device),
        Parameter({hiddenLayersDim}, 0.0f, device),
    };
    auto outputTimesParam = Parameter(NDArrayView::RandomUniform<float>({numOutputClasses, hiddenLayersDim}, -0.5, 0.5, 1, device));

    // Run evaluation in parallel    
    std::vector<std::thread> threadList(threadCount);
    for (int  th = 0; th < threadCount; ++th)
    {
        threadList[th] = std::thread(EvaluationNewNetworkWithSharedParameters, inputDim, numOutputClasses, numHiddenLayers, inputTimesParam, inputPlusParam, hiddenLayerTimesParam, hiddenLayerPlusParam, outputTimesParam, device);
    }

    for (int th = 0; th < threadCount; ++th)
    {
        threadList[th].join();
        fprintf(stderr, "thread %d joined.\n", th);
        fflush(stderr);
    }
}

inline CNTK::FunctionPtr SetupFullyConnectedLinearLayer(CNTK::Variable input, size_t outputDim, const CNTK::DeviceDescriptor& device, const std::wstring& outputName = L"")
{
    assert(input.Shape().Rank() == 1);
    size_t inputDim = input.Shape()[0];

    auto timesParam = CNTK::Parameter(CNTK::NDArrayView::RandomUniform<float>({outputDim, inputDim}, -0.05, 0.05, 1, device));
    auto timesFunction = CNTK::Times(timesParam, input);

    auto plusParam = CNTK::Parameter(CNTK::NDArrayView::RandomUniform<float>({outputDim}, -0.05, 0.05, 1, device));
    return CNTK::Plus(plusParam, timesFunction, outputName);
}

inline CNTK::FunctionPtr SetupFullyConnectedDNNLayer(CNTK::Variable input, size_t outputDim, const CNTK::DeviceDescriptor& device, const std::function<CNTK::FunctionPtr(const CNTK::FunctionPtr&)>& nonLinearity)
{
    return nonLinearity(SetupFullyConnectedLinearLayer(input, outputDim, device));
}

//void RunEvaluation(FunctionPtr evalFunc, const DeviceDescriptor& device)
//{
//
//    auto inputVariables= evalFunc->Arguments();
//    auto outputVariables = evalFunc->Outputs();
//
//    //// Evaluate the network in several runs 
//    //size_t iterationCount = 4;
//    //unsigned int randSeed = 2;
//    //srand(randSeed);
//    //size_t numSamples = 3;
//    //for (size_t t = 0; t < iterationCount; ++t)
//    //{
//    //    std::vector<float> inputData(inputDim * numSamples);
//    //    for (size_t i = 0; i < inputData.size(); ++i)
//    //        inputData[i] = ((float)rand()) / RAND_MAX;
//
//    //    NDShape inputShape = {inputDim, 1, numSamples};
//    //    ValuePtr inputValue = MakeSharedObject<Value>(MakeSharedObject<NDArrayView>(inputShape, inputData.data(), inputData.size(), DeviceDescriptor::CPUDevice(), true));
//
//    //    std::vector<float> labelData(numOutputClasses * numSamples, 0);
//    //    for (size_t i = 0; i < numSamples; ++i)
//    //        labelData[(i*numOutputClasses) + (rand() % numOutputClasses)] = 1;
//
//    //    NDShape labelShape = {numOutputClasses, 1, numSamples};
//    //    ValuePtr labelValue = MakeSharedObject<Value>(MakeSharedObject<NDArrayView>(labelShape, labelData.data(), labelData.size(), DeviceDescriptor::CPUDevice(), true));
//
//    //    ValuePtr outputValue, predictionErrorValue;
//    //    std::unordered_map<Variable, ValuePtr> outputs = {{classifierOutputFunction->Output(), outputValue}, {predictionFunction->Output(), predictionErrorValue}};
//    //    ffNet->Forward({{inputVar, inputValue}, {labelsVar, labelValue}}, outputs, computeDevice);
//    //}
//}

void EvalMultiThreadsWithClone(const DeviceDescriptor& device, const int threadCount)
{
    using namespace std::placeholders;

    const size_t inputDim = 937;
    const size_t numOutputClasses = 9304;
    const size_t numHiddenLayers = 6;
    const size_t hiddenLayersDim = 2048;

    auto inputVar = InputVariable({inputDim}, DataType::Float, L"features");

    assert(numHiddenLayers >= 1);
    auto classifierRoot = SetupFullyConnectedDNNLayer(inputVar, hiddenLayersDim, device, std::bind(Sigmoid, _1, L""));
    for (size_t i = 1; i < numHiddenLayers; ++i)
        classifierRoot = SetupFullyConnectedDNNLayer(classifierRoot, hiddenLayersDim, device, std::bind(Sigmoid, _1, L""));

    auto outputTimesParam = Parameter(NDArrayView::RandomUniform<float>({numOutputClasses, hiddenLayersDim}, -0.5, 0.5, 1, device));
    auto classifierOutput=  Times(outputTimesParam, classifierRoot, 1, L"classifierOutput");

    auto labelsVar = InputVariable({numOutputClasses}, DataType::Float, L"Labels");
    auto trainingLoss = CNTK::CrossEntropyWithSoftmax(classifierOutput, labelsVar, L"LossFunction");
    auto prediction = CNTK::ClassificationError(classifierOutput, labelsVar, L"ClassificationError");

    auto ffNet = CNTK::Combine({trainingLoss, prediction, classifierOutput}, L"ClassifierModel");

    // Now test the structure
    if (ffNet->Parameters().size() != ((numHiddenLayers * 2) + 1))
        throw std::runtime_error("TestFeedForwardNetworkCreation: Function does not have expected Parameter count");

    if (ffNet->Arguments().size() != 2)
        throw std::runtime_error("TestFeedForwardNetworkCreation: Function does not have expected Argument count");

    if (ffNet->Outputs().size() != 3)
        throw std::runtime_error("TestFeedForwardNetworkCreation: Function does not have expected Output count");

    auto count = threadCount;
    auto inputVariables = ffNet->Arguments();
    auto outputVariables = ffNet->Outputs();
    fprintf(stderr, "%d, %p, %p\n", count, inputVariables, outputVariables);

    // Run evaluation in parallel    
   /* std::vector<std::thread> threadList(threadCount);
    for (int th = 0; th < threadCount; ++th)
    {
        threadList[th] = std::thread(RunEvaluation, Clone(ffNet, ), device);
    }*/

   /* for (int th = 0; th < threadCount; ++th)
    {
        threadList[th].join();
        fprintf(stderr, "thread %d joined.\n", th);
        fflush(stderr);
    }*/

}
