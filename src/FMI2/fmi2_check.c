/*
    Copyright (C) 2012 Modelon AB <http://www.modelon.com>

	You should have received a copy of the LICENCE-FMUChecker.txt
    along with this program. If not, contact Modelon AB.
*/
/**
	\file fmi2_check.c
	Driver for FMI 2.0 checking and IO routines.
*/

#include <errno.h>
#include <assert.h>

#include <fmuChecker.h>
#include <fmilib.h>

void  fmi2_checker_logger(fmi2_component_t c, fmi2_string_t instanceName, fmi2_status_t status, fmi2_string_t category, fmi2_string_t message, ...){

	fmu_check_data_t* cdata = cdata_global_ptr;
	fmi2_import_t* fmu = cdata->fmu2;
	jm_callbacks* cb = &cdata->callbacks;
	jm_log_level_enu_t logLevel;
	char buf[10000], *curp = buf;
	const char* statusStr;
    va_list args;

	assert(cdata);
	assert(fmu);

	if(!cdata->printed_instance_name_error_flg) {
		if(strcmp(instanceName, cdata->instanceName) != 0) {
			jm_log_error(cb, fmu_checker_module, "FMU does not utilize provided instance name (%s != %s)", cdata->instanceName, instanceName);
			cdata->printed_instance_name_error_flg = 1;
		}
		else if(instanceName == cdata->instanceName) {
			jm_log_error(cb, fmu_checker_module, "FMU does not make an internal copy of provided instance name (violation of fmiString handling)",
				cdata->instanceName, instanceName);
			cdata->printed_instance_name_error_flg = 1;
		}
	}

	switch(status) {
		case fmi2_status_pending:
		case fmi2_status_ok:
			logLevel = jm_log_level_info;
			break;
		case fmi2_status_discard:
		case fmi2_status_warning:
			logLevel = jm_log_level_warning;
			break;
		case fmi2_status_error:
			logLevel = jm_log_level_error;
			break;
		case fmi2_status_fatal:
		default:
			logLevel = jm_log_level_fatal;
	}

    if(logLevel > cb->log_level) return;
	if(logLevel < jm_log_level_info)
		cdata->num_fmu_messages++;

	if(category && *category) {
        sprintf(curp, "\t[FMU][%s]", category);
    }
	else {
        sprintf(curp, "\t[FMU]");
	}
    curp += strlen(curp);
	statusStr = fmi2_status_to_string(status);
    sprintf(curp, "[FMU status:%s] ", statusStr);
    curp += strlen(statusStr) + strlen("[FMU status:] ");
    va_start (args, message);
	vsprintf(curp, message, args);
	fmi2_import_expand_variable_references(fmu, buf, cb->errMessageBuffer,JM_MAX_ERROR_MESSAGE_SIZE);
    va_end (args);
	checker_logger(cb, fmu_checker_module, jm_log_level_nothing, cb->errMessageBuffer);
}

int annotation_start_handle(void *context, const char *parentName, void *parent, const char *elm, const char **attr) {
	jm_callbacks* cb = (jm_callbacks*)context;
	int i = 0;
	jm_log_verbose(cb,fmu_checker_module, "Annotation element %s start (under %s:%s)\n", elm, parentName, 
		parent?fmi2_import_get_variable_name((fmi2_import_variable_t*)parent):"");
	while(attr[i]) {
		jm_log_verbose(cb,fmu_checker_module,"Attribute %s = %s\n", attr[i], attr[i+1]);
		i+=2;
	}
	return 0;
}

int annotation_data_handle(void* context, const char *s, int len) {
	return 0;
}

int annotation_end_handle(void *context, const char *elm) {	
	return 0;
}

jm_status_enu_t fmi2_check(fmu_check_data_t* cdata) {
	fmi2_callback_functions_t callBackFunctions;
	jm_callbacks* cb = &cdata->callbacks;
	jm_status_enu_t status = jm_status_success;

	cdata->fmu2 = fmi2_import_parse_xml(cdata->context, cdata->tmpPath, 0);

	if(!cdata->fmu2) {
		jm_log_fatal(cb,fmu_checker_module,"Error parsing XML, exiting");
		return jm_status_error;
	}

	cdata->modelName = fmi2_import_get_model_name(cdata->fmu2);
	cdata->GUID = fmi2_import_get_GUID(cdata->fmu2);

	jm_log_info(cb, fmu_checker_module,"Model name: %s", cdata->modelName);
    jm_log_info(cb, fmu_checker_module,"Model GUID: %s", cdata->GUID);
    jm_log_info(cb, fmu_checker_module,"Model version: %s", fmi2_import_get_model_version(cdata->fmu2));

	cdata->fmu2_kind = fmi2_import_get_fmu_kind(cdata->fmu2);

    jm_log_info(cb, fmu_checker_module,"FMU kind: %s", fmi2_fmu_kind_to_string(cdata->fmu2_kind));

	cdata->vl2 = fmi2_import_get_variable_list(cdata->fmu2);
	if(!cdata->vl2) {
		jm_log_fatal(cb, fmu_checker_module,"Could not construct model variables list");
		return jm_status_error;
	}

	if(cb->log_level >= jm_log_level_info) {
		fmi2_import_model_counts_t counts;
		fmi2_import_collect_model_counts(cdata->fmu2, &counts);
		jm_log_info(cb, fmu_checker_module,
			"The FMU contains:\n"
			"%u constants\n"
			"%u parameters\n"
			"%u discrete variables\n"
			"%u continuous variables\n"
			"%u inputs\n"
			"%u outputs\n"
			"%u local variables\n"
			"%u parameters\n"
			"%u real variables\n"
			"%u integer variables\n"
			"%u enumeration variables\n"
			"%u boolean variables\n"
			"%u string variables\n",
			counts.num_constants,
			counts.num_parameters,
			counts.num_discrete,
			counts.num_continuous,
			counts.num_inputs,
			counts.num_outputs,
			counts.num_local,
			counts.num_parameters,
			counts.num_real_vars,
			counts.num_integer_vars,
			counts.num_enum_vars,
			counts.num_bool_vars,
			counts.num_string_vars);
	}

	jm_log_info(cb, fmu_checker_module,"Printing output file header");
	if(fmi2_write_csv_header(cdata) != jm_status_success) {
		return jm_status_error;
	}

	if(!cdata->do_simulate_flg) {
		jm_log_verbose(cb, fmu_checker_module,"Simulation was not requested");
		return jm_status_success;
	}


	callBackFunctions.allocateMemory = check_calloc;
	callBackFunctions.freeMemory = check_free;
	callBackFunctions.logger = fmi2_checker_logger;
	callBackFunctions.stepFinished = 0;

	if( (cdata->fmu2_kind == fmi2_fmu_kind_me) || (cdata->fmu2_kind == fmi2_fmu_kind_me_and_cs)) {
		cdata->modelIdentifierME = fmi2_import_get_model_identifier_ME(cdata->fmu2);
	    jm_log_info(cb, fmu_checker_module,"Model identifier for ModelExchange: %s", cdata->modelIdentifierME);

		status = fmi2_import_create_dllfmu(cdata->fmu2, fmi2_fmu_kind_me, &callBackFunctions);

		if (status == jm_status_error) {
			jm_log_fatal(cb,fmu_checker_module,"Could not create the DLL loading mechanism(C-API) for ME.");
		}
		else {
			if(cdata->tmpPath == cdata->unzipPath) {
				fmi2_import_set_debug_mode(cdata->fmu2, 1);
			}
			jm_log_info(cb,fmu_checker_module,"Version returned from ME FMU:   %s\n", fmi2_import_get_version(cdata->fmu2));

			{
				const char* platform;

				platform= fmi2_import_get_types_platform(cdata->fmu2);

				if(strcmp(platform, fmi2_get_types_platform())) 
					jm_log_error(cb,fmu_checker_module,"Platform type returned from ME FMU %s does not match the checker  %s\n",platform, fmi2_get_types_platform() );
			}

			status = fmi2_me_simulate(cdata);
		}
	}
	if( (cdata->fmu2_kind == fmi2_fmu_kind_cs) || (cdata->fmu2_kind == fmi2_fmu_kind_me_and_cs)) {
		jm_status_enu_t savedStatus = status;
		cdata->modelIdentifierCS = fmi2_import_get_model_identifier_CS(cdata->fmu2);
	    jm_log_info(cb, fmu_checker_module,"Model identifier for CoSimulation: %s", cdata->modelIdentifierCS);
		status = fmi2_import_create_dllfmu(cdata->fmu2, fmi2_fmu_kind_cs, &callBackFunctions);

		if (status == jm_status_error) {
			jm_log_fatal(cb,fmu_checker_module,"Could not create the DLL loading mechanism(C-API) for CoSimulation.");
		}
		else {
			if(cdata->tmpPath == cdata->unzipPath) {
				fmi2_import_set_debug_mode(cdata->fmu2, 1);
			}
			jm_log_info(cb,fmu_checker_module,"Version returned from CS FMU:   %s\n", fmi2_import_get_version(cdata->fmu2));

			{
				const char* platform;

				platform= fmi2_import_get_types_platform(cdata->fmu2);

				if(strcmp(platform, fmi2_get_types_platform())) 
					jm_log_error(cb,fmu_checker_module,"Platform type returned from CS FMU %s does not match the checker  %s\n",platform, fmi2_get_types_platform() );
			}

			status = fmi2_cs_simulate(cdata);
		}
		if(status == jm_status_success) status = savedStatus;
		else if((status == jm_status_warning) && (savedStatus == jm_status_error)) status = jm_status_error;
	}
	return status;
}


jm_status_enu_t fmi2_write_csv_header(fmu_check_data_t* cdata) {
	fmi2_import_variable_list_t * vl = cdata->vl2;
	unsigned i, n = (unsigned)fmi2_import_get_variable_list_size(vl);

	char replace_sep = ':';
	
	if(cdata->CSV_separator == ':') {
		replace_sep = '|';
	}

	if(checked_fprintf(cdata,"time") != jm_status_success) {
		return jm_status_error;
	}

	for(i = 0; i < n; i++) {
		char buf[10000], *cursrc, *curdest;
		fmi2_import_variable_t * v = fmi2_import_get_variable(vl, i);
		const char* vn = fmi2_import_get_variable_name(v);
		fmi2_import_variable_t * vb = fmi2_import_get_variable_alias_base(cdata->fmu2, v);
		jm_status_enu_t status = jm_status_success;
		switch(fmi2_import_get_variable_alias_kind(v)) {
			case fmi2_variable_is_not_alias:
				sprintf(buf, "%s", vn);
				break;
			case fmi2_variable_is_alias:
				sprintf(buf, "%s=%s", vn, fmi2_import_get_variable_name(vb));
				break;
			default:
				assert(0);
			}
		/* skip spaces ans repace separator character in column names */
		curdest = cursrc = buf;
		while(*cursrc) {
			if(*cursrc != ' ') {
				if(*cursrc == cdata->CSV_separator)
					*curdest = replace_sep;
				else if(curdest != cursrc)
					*curdest = *cursrc;
				curdest++;
			}
			cursrc++;
		}
		*curdest = 0;
		status = checked_fprintf(cdata, "%c%s", cdata->CSV_separator, buf);
		if(status != jm_status_success) {
			return jm_status_error;
		}
	}

	if(checked_fprintf(cdata, "\n") != jm_status_success) {
		return jm_status_error;
	}
	return jm_status_success;
}

jm_status_enu_t fmi2_write_csv_data(fmu_check_data_t* cdata, double time) {
	fmi2_import_t* fmu = cdata->fmu2;
	fmi2_import_variable_list_t * vl = cdata->vl2;
	jm_callbacks* cb = &cdata->callbacks;
	fmi2_status_t fmistatus = fmi2_status_ok;
	jm_status_enu_t outstatus = jm_status_success;
	unsigned i, n = (unsigned)fmi2_import_get_variable_list_size(vl);

	char fmt_sep[2];
	char fmt_r[20];
	char fmt_i[20];
	char fmt_true[20];
	char fmt_false[20];
	fmt_sep[0] = cdata->CSV_separator; fmt_sep[1] = 0;
	sprintf(fmt_r, "%c%s", cdata->CSV_separator, "%g");
	sprintf(fmt_i, "%c%s", cdata->CSV_separator, "%d");
	if(cdata->out_enum_as_int_flag) {
		sprintf(fmt_true, "%c1", cdata->CSV_separator);
		sprintf(fmt_false, "%c0", cdata->CSV_separator);
	}
	else
	{
		sprintf(fmt_true, "%ctrue", cdata->CSV_separator);
		sprintf(fmt_false, "%cfalse", cdata->CSV_separator);
	}

	if(checked_fprintf(cdata, "%g", time) != jm_status_success) {
		return jm_status_error;
	}

	for(i = 0; i < n; i++) {
		fmi2_import_variable_t* v = fmi2_import_get_variable(vl, i);
		fmi2_value_reference_t vr = fmi2_import_get_variable_vr(v); 
		switch(fmi2_import_get_variable_base_type(v)) {
		case fmi2_base_type_real:
			{
				double val;
				fmistatus = fmi2_import_get_real(fmu,&vr, 1, &val);
				outstatus = checked_fprintf(cdata, fmt_r, val);
				break;
			}
		case fmi2_base_type_int:
			{
				int val;
				fmistatus = fmi2_import_get_integer(fmu,&vr, 1, &val);
				outstatus = checked_fprintf(cdata, fmt_i, val);
				break;
			}
		case fmi2_base_type_bool:
			{
				fmi2_boolean_t val;
				char* fmt;
				fmistatus = fmi2_import_get_boolean(fmu,&vr, 1, &val);
				
				fmt = (val == fmi2_true) ? fmt_true:fmt_false;
				outstatus = checked_fprintf(cdata, fmt);
				break;
			}
		case fmi2_base_type_str:
			{
				fmi2_string_t val;
				
				fmistatus = fmi2_import_get_string(fmu,&vr, 1, &val);
				checked_fprintf(cdata, fmt_sep);
				outstatus = checked_print_quoted_str(cdata, val);
				break;
			}
		case fmi2_base_type_enum:
			{
				int val;
				fmi2_import_variable_typedef_t* t = fmi2_import_get_variable_declared_type(v);
				fmi2_import_enumeration_typedef_t* et = 0;
				unsigned int item = 0;
				const char* itname = 0;
				if(t) et = fmi2_import_get_type_as_enum(t);

				fmistatus = fmi2_import_get_integer(fmu,&vr, 1, &val);
				if(et) itname = fmi2_import_get_enum_type_item_name(et, val);
				if(!itname) {
					jm_log_error(cb, fmu_checker_module, "Could not get item name for enum variable %s", fmi2_import_get_variable_name(v));
				}
				if(cdata->out_enum_as_int_flag || !itname) {
					outstatus = checked_fprintf(cdata, fmt_i, val);
				}
				else {
					checked_fprintf(cdata, fmt_sep);
					outstatus = checked_print_quoted_str(cdata, itname);
				}
				break;
			}
		}
		if(  fmistatus != fmi2_status_ok) {
			jm_log_warning(cb, fmu_checker_module, "fmiGetXXX returned status: %s for variable %s", 
				fmi2_status_to_string(fmistatus), fmi2_import_get_variable_name(v));
		}

		if(outstatus != jm_status_success) {
			return jm_status_error;
		}
	}
	if(checked_fprintf(cdata, "\n")!= jm_status_success) {
		return jm_status_error;
	}
	return jm_status_success;
}