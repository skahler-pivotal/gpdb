//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Greenplum, Inc.
//
//	@filename:
//		COptTasks.cpp
//
//	@doc:
//		Routines to perform optimization related tasks using the gpos framework
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpopt/utils/gpdbdefs.h"
#include "gpopt/utils/CConstExprEvaluatorProxy.h"
#include "gpopt/utils/COptTasks.h"
#include "gpopt/relcache/CMDProviderRelcache.h"
#include "gpopt/config/CConfigParamMapping.h"
#include "gpopt/translate/CTranslatorDXLToExpr.h"
#include "gpopt/translate/CTranslatorExprToDXL.h"
#include "gpopt/translate/CTranslatorUtils.h"
#include "gpopt/translate/CTranslatorQueryToDXL.h"
#include "gpopt/translate/CTranslatorDXLToPlStmt.h"
#include "gpopt/translate/CContextDXLToPlStmt.h"
#include "gpopt/translate/CTranslatorRelcacheToDXL.h"
#include "gpopt/eval/CConstExprEvaluatorDXL.h"
#include "gpopt/engine/CHint.h"

#include "cdb/cdbvars.h"
#include "utils/guc.h"
#include "utils/fmgroids.h"

#include "gpos/base.h"
#include "gpos/error/CException.h"
#undef setstate

#include "gpos/_api.h"
#include "gpos/common/CAutoP.h"
#include "gpos/io/COstreamFile.h"
#include "gpos/io/COstreamString.h"
#include "gpos/memory/CAutoMemoryPool.h"
#include "gpos/task/CAutoTraceFlag.h"
#include "gpos/common/CAutoP.h"

#include "gpopt/translate/CTranslatorDXLToExpr.h"
#include "gpopt/translate/CTranslatorExprToDXL.h"

#include "gpopt/base/CAutoOptCtxt.h"
#include "gpopt/engine/CEnumeratorConfig.h"
#include "gpopt/engine/CStatisticsConfig.h"
#include "gpopt/engine/CCTEConfig.h"
#include "gpopt/mdcache/CAutoMDAccessor.h"
#include "gpopt/mdcache/CMDCache.h"
#include "gpopt/minidump/CMinidumperUtils.h"
#include "gpopt/optimizer/COptimizer.h"
#include "gpopt/optimizer/COptimizerConfig.h"
#include "gpopt/xforms/CXformFactory.h"
#include "gpopt/exception.h"

#include "naucrates/init.h"
#include "naucrates/traceflags/traceflags.h"

#include "naucrates/base/CQueryToDXLResult.h"

#include "naucrates/md/IMDId.h"
#include "naucrates/md/CMDIdRelStats.h"

#include "naucrates/md/CSystemId.h"
#include "naucrates/md/IMDRelStats.h"
#include "naucrates/md/CMDIdCast.h"
#include "naucrates/md/CMDIdScCmp.h"

#include "naucrates/dxl/operators/CDXLNode.h"
#include "naucrates/dxl/parser/CParseHandlerDXL.h"
#include "naucrates/dxl/CDXLUtils.h"
#include "naucrates/dxl/CIdGenerator.h"
#include "naucrates/exception.h"

#include "gpdbcost/CCostModelGPDB.h"
#include "gpdbcost/CCostModelGPDBLegacy.h"


#include "gpopt/gpdbwrappers.h"

using namespace gpos;
using namespace gpopt;
using namespace gpdxl;
using namespace gpdbcost;

// size of error buffer
#define GPOPT_ERROR_BUFFER_SIZE 10 * 1024 * 1024

// definition of default AutoMemoryPool
#define AUTO_MEM_POOL(amp) CAutoMemoryPool amp(CAutoMemoryPool::ElcExc, CMemoryPoolManager::EatTracker, false /* fThreadSafe */)

// default id for the source system
const CSystemId default_sysid(IMDId::EmdidGPDB, GPOS_WSZ_STR_LENGTH("GPDB"));

// array of optimizer minor exception types that trigger expected fallback to the planner
const ULONG expected_opt_fallback[] =
	{
		gpopt::ExmiInvalidPlanAlternative,		// chosen plan id is outside range of possible plans
		gpopt::ExmiUnsupportedOp,				// unsupported operator
		gpopt::ExmiUnsupportedPred,				// unsupported predicate
		gpopt::ExmiUnsupportedCompositePartKey	// composite partitioning keys
	};

// array of DXL minor exception types that trigger expected fallback to the planner
const ULONG expected_dxl_fallback[] =
	{
		gpdxl::ExmiMDObjUnsupported,			// unsupported metadata object
		gpdxl::ExmiQuery2DXLUnsupportedFeature,	// unsupported feature during algebrization
		gpdxl::ExmiPlStmt2DXLConversion,		// unsupported feature during plan freezing
		gpdxl::ExmiDXL2PlStmtConversion			// unsupported feature during planned statement translation
	};

// array of DXL minor exception types that error out and NOT fallback to the planner
const ULONG expected_dxl_errors[] =
	{
		gpdxl::ExmiDXL2PlStmtExternalScanError,	// external table error
		gpdxl::ExmiQuery2DXLNotNullViolation,	// not null violation
	};


//---------------------------------------------------------------------------
//	@function:
//		SOptContext::SOptContext
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
SOptContext::SOptContext()
	:
	m_query_dxl(NULL),
	m_query(NULL),
	m_plan_dxl(NULL),
	m_plan_stmt(NULL),
	m_should_generate_plan_stmt(false),
	m_should_serialize_plan_dxl(false),
	m_is_unexpected_failure(false),
	m_should_error_out(false),
	m_error_msg(NULL)
{}

//---------------------------------------------------------------------------
//	@function:
//		SOptContext::Free
//
//	@doc:
//		Free all members except those pointed to by either input or
//		output
//
//---------------------------------------------------------------------------
void
SOptContext::Free
	(
	SOptContext::EPin input,
	SOptContext::EPin output
	)
{
	if (NULL != m_query_dxl && epinQueryDXL != input && epinQueryDXL != output)
	{
		gpdb::GPDBFree(m_query_dxl);
	}
	
	if (NULL != m_query && epinQuery != input && epinQuery != output)
	{
		gpdb::GPDBFree(m_query);
	}
	
	if (NULL != m_plan_dxl && epinPlanDXL != input && epinPlanDXL != output)
	{
		gpdb::GPDBFree(m_plan_dxl);
	}
	
	if (NULL != m_plan_stmt && epinPlStmt != input && epinPlStmt != output)
	{
		gpdb::GPDBFree(m_plan_stmt);
	}
	
	if (NULL != m_error_msg && epinErrorMsg != input && epinErrorMsg != output)
	{
		gpdb::GPDBFree(m_error_msg);
	}
}

//---------------------------------------------------------------------------
//	@function:
//		SOptContext::CloneErrorMsg
//
//	@doc:
//		Clone m_error_msg to given memory context. Return NULL if there is no
//		error message.
//
//---------------------------------------------------------------------------
CHAR*
SOptContext::CloneErrorMsg
	(
	MemoryContext context
	)
{
	if (NULL == context ||
		NULL == m_error_msg)
	{
		return NULL;
	}
	return gpdb::MemCtxtStrdup(context, m_error_msg);
}


//---------------------------------------------------------------------------
//	@function:
//		SOptContext::Cast
//
//	@doc:
//		Casting function
//
//---------------------------------------------------------------------------
SOptContext *
SOptContext::Cast
	(
	void *ptr
	)
{
	GPOS_ASSERT(NULL != ptr);

	return reinterpret_cast<SOptContext*>(ptr);
}

//---------------------------------------------------------------------------
//	@function:
//		CreateMultiByteCharStringFromWCString
//
//	@doc:
//		Return regular string from wide-character string
//
//---------------------------------------------------------------------------
CHAR *
COptTasks::CreateMultiByteCharStringFromWCString
	(
	const WCHAR *wcstr
	)
{
	GPOS_ASSERT(NULL != wcstr);

	const ULONG input_len = GPOS_WSZ_LENGTH(wcstr);
	const ULONG wchar_size = GPOS_SIZEOF(WCHAR);
	const ULONG max_len = (input_len + 1) * wchar_size;

	CHAR *str = (CHAR *) gpdb::GPDBAlloc(max_len);

	gpos::clib::Wcstombs(str, const_cast<WCHAR *>(wcstr), max_len);
	str[max_len - 1] = '\0';

	return str;
}

//---------------------------------------------------------------------------
//	@function:
//		COptTasks::Execute
//
//	@doc:
//		Execute a task using GPOS. TODO extend gpos to provide
//		this functionality
//
//---------------------------------------------------------------------------
void
COptTasks::Execute
	(
	void *(*func) (void *) ,
	void *func_arg
	)
{
	Assert(func);

	CHAR *err_buf = (CHAR *) palloc(GPOPT_ERROR_BUFFER_SIZE);
	err_buf[0] = '\0';

	// initialize DXL support
	InitDXL();

	bool abort_flag = false;

	CAutoMemoryPool amp(CAutoMemoryPool::ElcNone, CMemoryPoolManager::EatTracker, false /* fThreadSafe */);

	gpos_exec_params params;
	params.func = func;
	params.arg = func_arg;
	params.stack_start = &params;
	params.error_buffer = err_buf;
	params.error_buffer_size = GPOPT_ERROR_BUFFER_SIZE;
	params.abort_requested = &abort_flag;

	// execute task and send log message to server log
	GPOS_TRY
	{
		(void) gpos_exec(&params);
	}
	GPOS_CATCH_EX(ex)
	{
		LogExceptionMessageAndDelete(err_buf, ex.SeverityLevel());
		GPOS_RETHROW(ex);
	}
	GPOS_CATCH_END;
	LogExceptionMessageAndDelete(err_buf);
}

void
COptTasks::LogExceptionMessageAndDelete(CHAR* err_buf, ULONG severity_level)
{

	if ('\0' != err_buf[0])
	{
		int gpdb_severity_level;

		if (severity_level == CException::ExsevDebug1)
			gpdb_severity_level = DEBUG1;
		else
			gpdb_severity_level = LOG;

		elog(gpdb_severity_level, "%s", CreateMultiByteCharStringFromWCString((WCHAR *)err_buf));
	}

	pfree(err_buf);
}


//---------------------------------------------------------------------------
//	@function:
//		COptTasks::ConvertToPlanStmtFromDXL
//
//	@doc:
//		Translate a DXL tree into a planned statement
//
//---------------------------------------------------------------------------
PlannedStmt *
COptTasks::ConvertToPlanStmtFromDXL
	(
	IMemoryPool *mp,
	CMDAccessor *md_accessor,
	const CDXLNode *dxlnode,
	bool can_set_tag,
	DistributionHashOpsKind distribution_hashops
	)
{

	GPOS_ASSERT(NULL != md_accessor);
	GPOS_ASSERT(NULL != dxlnode);

	CIdGenerator plan_id_generator(1 /* ulStartId */);
	CIdGenerator motion_id_generator(1 /* ulStartId */);
	CIdGenerator param_id_generator(0 /* ulStartId */);

	List *table_list = NULL;
	List *subplans_list = NULL;

	CContextDXLToPlStmt dxl_to_plan_stmt_ctxt
							(
							mp,
							&plan_id_generator,
							&motion_id_generator,
							&param_id_generator,
							distribution_hashops,
							&table_list,
							&subplans_list
							);
	
	// translate DXL -> PlannedStmt
	CTranslatorDXLToPlStmt dxl_to_plan_stmt_translator(mp, md_accessor, &dxl_to_plan_stmt_ctxt, gpdb::GetGPSegmentCount());
	return dxl_to_plan_stmt_translator.GetPlannedStmtFromDXL(dxlnode, can_set_tag);
}


//---------------------------------------------------------------------------
//	@function:
//		COptTasks::LoadSearchStrategy
//
//	@doc:
//		Load search strategy from given file
//
//---------------------------------------------------------------------------
CSearchStageArray *
COptTasks::LoadSearchStrategy
	(
	IMemoryPool *mp,
	char *path
	)
{
	CSearchStageArray *search_strategy_arr = NULL;
	CParseHandlerDXL *dxl_parse_handler = NULL;

	GPOS_TRY
	{
		if (NULL != path)
		{
			dxl_parse_handler = CDXLUtils::GetParseHandlerForDXLFile(mp, path, NULL);
			if (NULL != dxl_parse_handler)
			{
				elog(DEBUG2, "\n[OPT]: Using search strategy in (%s)", path);

				search_strategy_arr = dxl_parse_handler->GetSearchStageArray();
				search_strategy_arr->AddRef();
			}
		}
	}
	GPOS_CATCH_EX(ex)
	{
		if (GPOS_MATCH_EX(ex, gpdxl::ExmaGPDB, gpdxl::ExmiGPDBError)) {
			GPOS_RETHROW(ex);
		}
		elog(DEBUG2, "\n[OPT]: Using default search strategy");
		GPOS_RESET_EX;
	}
	GPOS_CATCH_END;

	GPOS_DELETE(dxl_parse_handler);

	return search_strategy_arr;
}

//---------------------------------------------------------------------------
//	@function:
//		COptTasks::CreateOptimizerConfig
//
//	@doc:
//		Create the optimizer configuration
//
//---------------------------------------------------------------------------
COptimizerConfig *
COptTasks::CreateOptimizerConfig
	(
	IMemoryPool *mp,
	ICostModel *cost_model
	)
{
	// get chosen plan number, cost threshold
	ULLONG plan_id = (ULLONG) optimizer_plan_id;
	ULLONG num_samples = (ULLONG) optimizer_samples_number;
	DOUBLE cost_threshold = (DOUBLE) optimizer_cost_threshold;

	DOUBLE damping_factor_filter = (DOUBLE) optimizer_damping_factor_filter;
	DOUBLE damping_factor_join = (DOUBLE) optimizer_damping_factor_join;
	DOUBLE damping_factor_groupby = (DOUBLE) optimizer_damping_factor_groupby;

	ULONG cte_inlining_cutoff = (ULONG) optimizer_cte_inlining_bound;
	ULONG join_arity_for_associativity_commutativity = (ULONG) optimizer_join_arity_for_associativity_commutativity;
	ULONG array_expansion_threshold = (ULONG) optimizer_array_expansion_threshold;
	ULONG join_order_threshold = (ULONG) optimizer_join_order_threshold;
	ULONG broadcast_threshold = (ULONG) optimizer_penalize_broadcast_threshold;

	return GPOS_NEW(mp) COptimizerConfig
						(
						GPOS_NEW(mp) CEnumeratorConfig(mp, plan_id, num_samples, cost_threshold),
						GPOS_NEW(mp) CStatisticsConfig(mp, damping_factor_filter, damping_factor_join, damping_factor_groupby),
						GPOS_NEW(mp) CCTEConfig(cte_inlining_cutoff),
						cost_model,
						GPOS_NEW(mp) CHint
								(
								gpos::int_max /* optimizer_parts_to_force_sort_on_insert */,
								join_arity_for_associativity_commutativity,
								array_expansion_threshold,
								join_order_threshold,
								broadcast_threshold,
								false /* don't create Assert nodes for constraints, we'll
								      * enforce them ourselves in the executor */
								),
						GPOS_NEW(mp) CWindowOids(OID(F_WINDOW_ROW_NUMBER), OID(F_WINDOW_RANK))
						);
}


//---------------------------------------------------------------------------
//	@function:
//		COptTasks::FoundException
//
//	@doc:
//		Lookup given exception type in the given array
//
//---------------------------------------------------------------------------
BOOL
COptTasks::FoundException
	(
	gpos::CException &exc,
	const ULONG *exceptions,
	ULONG size
	)
{
	GPOS_ASSERT(NULL != exceptions);

	ULONG minor = exc.Minor();
	BOOL found = false;
	for (ULONG ul = 0; !found && ul < size; ul++)
	{
		found = (exceptions[ul] == minor);
	}

	return found;
}


//---------------------------------------------------------------------------
//	@function:
//		COptTasks::IsUnexpectedFailure
//
//	@doc:
//		Check if given exception is an unexpected reason for failing to
//		produce a plan
//
//---------------------------------------------------------------------------
BOOL
COptTasks::IsUnexpectedFailure
	(
	gpos::CException &exc
	)
{
	ULONG major = exc.Major();

	BOOL is_opt_failure_expected =
		gpopt::ExmaGPOPT == major &&
		FoundException(exc, expected_opt_fallback, GPOS_ARRAY_SIZE(expected_opt_fallback));

	BOOL is_dxl_failure_expected =
		(gpdxl::ExmaDXL == major || gpdxl::ExmaMD == major) &&
		FoundException(exc, expected_dxl_fallback, GPOS_ARRAY_SIZE(expected_dxl_fallback));

	return (!is_opt_failure_expected && !is_dxl_failure_expected);
}

//---------------------------------------------------------------------------
//	@function:
//		COptTasks::ShouldErrorOut
//
//	@doc:
//		Check if given exception should error out
//
//---------------------------------------------------------------------------
BOOL
COptTasks::ShouldErrorOut
	(
	gpos::CException &exc
	)
{
	return
		gpdxl::ExmaDXL == exc.Major() &&
		FoundException(exc, expected_dxl_errors, GPOS_ARRAY_SIZE(expected_dxl_errors));
}

//---------------------------------------------------------------------------
//		@function:
//			COptTasks::SetCostModelParams
//
//      @doc:
//			Set cost model parameters
//
//---------------------------------------------------------------------------
void
COptTasks::SetCostModelParams
	(
	ICostModel *cost_model
	)
{
	GPOS_ASSERT(NULL != cost_model);

	if (optimizer_nestloop_factor > 1.0)
	{
		// change NLJ cost factor
		ICostModelParams::SCostParam *cost_param = NULL;
		if (OPTIMIZER_GPDB_CALIBRATED == optimizer_cost_model)
		{
			cost_param = cost_model->GetCostModelParams()->PcpLookup(CCostModelParamsGPDB::EcpNLJFactor);
		}
		else
		{
			cost_param = cost_model->GetCostModelParams()->PcpLookup(CCostModelParamsGPDBLegacy::EcpNLJFactor);
		}
		CDouble nlj_factor(optimizer_nestloop_factor);
		cost_model->GetCostModelParams()->SetParam(cost_param->Id(), nlj_factor, nlj_factor - 0.5, nlj_factor + 0.5);
	}

	if (optimizer_sort_factor > 1.0 || optimizer_sort_factor < 1.0)
	{
		// change sort cost factor
		ICostModelParams::SCostParam *cost_param = NULL;
		if (OPTIMIZER_GPDB_CALIBRATED == optimizer_cost_model)
		{
			cost_param = cost_model->GetCostModelParams()->PcpLookup(CCostModelParamsGPDB::EcpSortTupWidthCostUnit);

			CDouble sort_factor(optimizer_sort_factor);
			cost_model->GetCostModelParams()->SetParam(cost_param->Id(), cost_param->Get() * optimizer_sort_factor, cost_param->GetLowerBoundVal() * optimizer_sort_factor, cost_param->GetUpperBoundVal() * optimizer_sort_factor);
		}
	}
}


//---------------------------------------------------------------------------
//      @function:
//			COptTasks::GetCostModel
//
//      @doc:
//			Generate an instance of optimizer cost model
//
//---------------------------------------------------------------------------
ICostModel *
COptTasks::GetCostModel
	(
	IMemoryPool *mp,
	ULONG num_segments
	)
{
	ICostModel *cost_model = NULL;
	if (OPTIMIZER_GPDB_CALIBRATED == optimizer_cost_model)
	{
		cost_model = GPOS_NEW(mp) CCostModelGPDB(mp, num_segments);
	}
	else
	{
		cost_model = GPOS_NEW(mp) CCostModelGPDBLegacy(mp, num_segments);
	}

	SetCostModelParams(cost_model);

	return cost_model;
}

//---------------------------------------------------------------------------
//	@function:
//		COptTasks::OptimizeTask
//
//	@doc:
//		task that does the optimizes query to physical DXL
//
//---------------------------------------------------------------------------
void*
COptTasks::OptimizeTask
	(
	void *ptr
	)
{
	GPOS_ASSERT(NULL != ptr);
	SOptContext *opt_ctxt = SOptContext::Cast(ptr);

	GPOS_ASSERT(NULL != opt_ctxt->m_query);
	GPOS_ASSERT(NULL == opt_ctxt->m_plan_dxl);
	GPOS_ASSERT(NULL == opt_ctxt->m_plan_stmt);

	AUTO_MEM_POOL(amp);
	IMemoryPool *mp = amp.Pmp();

	// Does the metadatacache need to be reset?
	//
	// On the first call, before the cache has been initialized, we
	// don't care about the return value of MDCacheNeedsReset(). But
	// we need to call it anyway, to give it a chance to initialize
	// the invalidation mechanism.
	bool reset_mdcache = gpdb::MDCacheNeedsReset();

	// initialize metadata cache, or purge if needed, or change size if requested
	if (!CMDCache::FInitialized())
	{
		CMDCache::Init();
		CMDCache::SetCacheQuota(optimizer_mdcache_size * 1024L);
	}
	else if (reset_mdcache)
	{
		CMDCache::Reset();
		CMDCache::SetCacheQuota(optimizer_mdcache_size * 1024L);
	}
	else if (CMDCache::ULLGetCacheQuota() != (ULLONG) optimizer_mdcache_size * 1024L)
	{
		CMDCache::SetCacheQuota(optimizer_mdcache_size * 1024L);
	}


	// load search strategy
	CSearchStageArray *search_strategy_arr = LoadSearchStrategy(mp, optimizer_search_strategy_path);

	CBitSet *trace_flags = NULL;
	CBitSet *enabled_trace_flags = NULL;
	CBitSet *disabled_trace_flags = NULL;
	CDXLNode *plan_dxl = NULL;

	IMdIdArray *col_stats = NULL;
	MdidHashSet *rel_stats = NULL;

	GPOS_TRY
	{
		// set trace flags
		trace_flags = CConfigParamMapping::PackConfigParamInBitset(mp, CXform::ExfSentinel);
		SetTraceflags(mp, trace_flags, &enabled_trace_flags, &disabled_trace_flags);

		// set up relcache MD provider
		CMDProviderRelcache *relcache_provider = GPOS_NEW(mp) CMDProviderRelcache(mp);

		{
			// scope for MD accessor
			CMDAccessor mda(mp, CMDCache::Pcache(), default_sysid, relcache_provider);

			ULONG num_segments = gpdb::GetGPSegmentCount();
			ULONG num_segments_for_costing = optimizer_segments;
			if (0 == num_segments_for_costing)
			{
				num_segments_for_costing = num_segments;
			}

			CAutoP<CTranslatorQueryToDXL> query_to_dxl_translator;
			query_to_dxl_translator = CTranslatorQueryToDXL::QueryToDXLInstance
							(
							mp,
							&mda,
							(Query*) opt_ctxt->m_query
							);

			ICostModel *cost_model = GetCostModel(mp, num_segments_for_costing);
			COptimizerConfig *optimizer_config = CreateOptimizerConfig(mp, cost_model);
			CConstExprEvaluatorProxy expr_eval_proxy(mp, &mda);
			IConstExprEvaluator *expr_evaluator =
					GPOS_NEW(mp) CConstExprEvaluatorDXL(mp, &mda, &expr_eval_proxy);

			CDXLNode *query_dxl = query_to_dxl_translator->TranslateQueryToDXL();
			CDXLNodeArray *query_output_dxlnode_array = query_to_dxl_translator->GetQueryOutputCols();
			CDXLNodeArray *cte_dxlnode_array = query_to_dxl_translator->GetCTEs();
			GPOS_ASSERT(NULL != query_output_dxlnode_array);

			BOOL is_master_only = !optimizer_enable_motions ||
						(!optimizer_enable_motions_masteronly_queries && !query_to_dxl_translator->HasDistributedTables());
			CAutoTraceFlag atf(EopttraceDisableMotions, is_master_only);

			plan_dxl = COptimizer::PdxlnOptimize
									(
									mp,
									&mda,
									query_dxl,
									query_output_dxlnode_array,
									cte_dxlnode_array,
									expr_evaluator,
									num_segments,
									gp_session_id,
									gp_command_count,
									search_strategy_arr,
									optimizer_config
									);

			if (opt_ctxt->m_should_serialize_plan_dxl)
			{
				// serialize DXL to xml
				CWStringDynamic plan_str(mp);
				COstreamString oss(&plan_str);
				CDXLUtils::SerializePlan(mp, oss, plan_dxl, optimizer_config->GetEnumeratorCfg()->GetPlanId(), optimizer_config->GetEnumeratorCfg()->GetPlanSpaceSize(), true /*serialize_header_footer*/, true /*indentation*/);
				opt_ctxt->m_plan_dxl = CreateMultiByteCharStringFromWCString(plan_str.GetBuffer());
			}

			// translate DXL->PlStmt only when needed
			if (opt_ctxt->m_should_generate_plan_stmt)
			{
				// always use opt_ctxt->m_query->can_set_tag as the query_to_dxl_translator->Pquery() is a mutated Query object
				// that may not have the correct can_set_tag
			  opt_ctxt->m_plan_stmt = (PlannedStmt *) gpdb::CopyObject(ConvertToPlanStmtFromDXL(mp, &mda, plan_dxl, opt_ctxt->m_query->canSetTag, query_to_dxl_translator->GetDistributionHashOpsKind()));
			}

			CStatisticsConfig *stats_conf = optimizer_config->GetStatsConf();
			col_stats = GPOS_NEW(mp) IMdIdArray(mp);
			stats_conf->CollectMissingStatsColumns(col_stats);

			rel_stats = GPOS_NEW(mp) MdidHashSet(mp);
			PrintMissingStatsWarning(mp, &mda, col_stats, rel_stats);

			rel_stats->Release();
			col_stats->Release();

			expr_evaluator->Release();
			query_dxl->Release();
			optimizer_config->Release();
			plan_dxl->Release();
		}
	}
	GPOS_CATCH_EX(ex)
	{
		ResetTraceflags(enabled_trace_flags, disabled_trace_flags);
		CRefCount::SafeRelease(rel_stats);
		CRefCount::SafeRelease(col_stats);
		CRefCount::SafeRelease(enabled_trace_flags);
		CRefCount::SafeRelease(disabled_trace_flags);
		CRefCount::SafeRelease(trace_flags);
		CRefCount::SafeRelease(plan_dxl);
		CMDCache::Shutdown();

		IErrorContext *errctxt = CTask::Self()->GetErrCtxt();

		opt_ctxt->m_should_error_out = ShouldErrorOut(ex);
		opt_ctxt->m_is_unexpected_failure = IsUnexpectedFailure(ex);
		opt_ctxt->m_error_msg = CreateMultiByteCharStringFromWCString(errctxt->GetErrorMsg());

		GPOS_RETHROW(ex);
	}
	GPOS_CATCH_END;

	// cleanup
	ResetTraceflags(enabled_trace_flags, disabled_trace_flags);
	CRefCount::SafeRelease(enabled_trace_flags);
	CRefCount::SafeRelease(disabled_trace_flags);
	CRefCount::SafeRelease(trace_flags);
	if (!optimizer_metadata_caching)
	{
		CMDCache::Shutdown();
	}

	return NULL;
}


//---------------------------------------------------------------------------
//	@function:
//		COptTasks::PrintMissingStatsWarning
//
//	@doc:
//		Print warning messages for columns with missing statistics
//
//---------------------------------------------------------------------------
void
COptTasks::PrintMissingStatsWarning
	(
	IMemoryPool *mp,
	CMDAccessor *md_accessor,
	IMdIdArray *col_stats,
	MdidHashSet *rel_stats
	)
{
	GPOS_ASSERT(NULL != md_accessor);
	GPOS_ASSERT(NULL != col_stats);
	GPOS_ASSERT(NULL != rel_stats);

	CWStringDynamic wcstr(mp);
	COstreamString oss(&wcstr);

	const ULONG num_missing_col_stats = col_stats->Size();
	for (ULONG ul = 0; ul < num_missing_col_stats; ul++)
	{
		IMDId *mdid = (*col_stats)[ul];
		CMDIdColStats *mdid_col_stats = CMDIdColStats::CastMdid(mdid);

		IMDId *rel_mdid = mdid_col_stats->GetRelMdId();
		const ULONG pos = mdid_col_stats->Position();
		const IMDRelation *rel = md_accessor->RetrieveRel(rel_mdid);

		if (IMDRelation::ErelstorageExternal != rel->RetrieveRelStorageType())
		{
			if (!rel_stats->Contains(rel_mdid))
			{
				if (0 != ul)
				{
					oss << ", ";
				}

				rel_mdid->AddRef();
				rel_stats->Insert(rel_mdid);
				oss << rel->Mdname().GetMDName()->GetBuffer();
			}

			CMDName mdname = rel->GetMdCol(pos)->Mdname();

			char msgbuf[NAMEDATALEN * 2 + 100];
			snprintf(msgbuf, sizeof(msgbuf), "Missing statistics for column: %s.%s", CreateMultiByteCharStringFromWCString(rel->Mdname().GetMDName()->GetBuffer()), CreateMultiByteCharStringFromWCString(mdname.GetMDName()->GetBuffer()));
			GpdbEreport(ERRCODE_SUCCESSFUL_COMPLETION,
						   LOG,
						   msgbuf,
						   NULL);
		}
	}

	if (0 < rel_stats->Size())
	{
		int length = NAMEDATALEN * rel_stats->Size() + 200;
		char msgbuf[length];
		snprintf(msgbuf, sizeof(msgbuf), "One or more columns in the following table(s) do not have statistics: %s", CreateMultiByteCharStringFromWCString(wcstr.GetBuffer()));
		GpdbEreport(ERRCODE_SUCCESSFUL_COMPLETION,
					   NOTICE,
					   msgbuf,
					   "For non-partitioned tables, run analyze <table_name>(<column_list>)."
					   " For partitioned tables, run analyze rootpartition <table_name>(<column_list>)."
					   " See log for columns missing statistics.");
	}

}

//---------------------------------------------------------------------------
//	@function:
//		COptTasks::Optimize
//
//	@doc:
//		optimizes a query to physical DXL
//
//---------------------------------------------------------------------------
char *
COptTasks::Optimize
	(
	Query *query
	)
{
	Assert(query);

	SOptContext gpopt_context;
	gpopt_context.m_query = query;
	gpopt_context.m_should_serialize_plan_dxl = true;
	Execute(&OptimizeTask, &gpopt_context);

	// clean up context
	gpopt_context.Free(gpopt_context.epinQuery, gpopt_context.epinPlanDXL);

	return gpopt_context.m_plan_dxl;
}


//---------------------------------------------------------------------------
//	@function:
//		COptTasks::GPOPTOptimizedPlan
//
//	@doc:
//		optimizes a query to plannedstmt
//
//---------------------------------------------------------------------------
PlannedStmt *
COptTasks::GPOPTOptimizedPlan
	(
	Query *query,
	SOptContext *gpopt_context
	)
{
	Assert(query);
	Assert(gpopt_context);

	gpopt_context->m_query = query;
	gpopt_context->m_should_generate_plan_stmt= true;
	Execute(&OptimizeTask, gpopt_context);
	return gpopt_context->m_plan_stmt;
}

//---------------------------------------------------------------------------
//	@function:
//		COptTasks::SetXform
//
//	@doc:
//		Enable/Disable a given xform
//
//---------------------------------------------------------------------------
bool
COptTasks::SetXform
	(
	char *xform_str,
	bool should_disable
	)
{
	CXform *xform = CXformFactory::Pxff()->Pxf(xform_str);
	if (NULL != xform)
	{
		optimizer_xforms[xform->Exfid()] = should_disable;

		return true;
	}

	return false;
}

// EOF
