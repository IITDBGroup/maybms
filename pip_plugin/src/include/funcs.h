#ifndef PIP_FUNCS_H_SHIELD
#define PIP_FUNCS_H_SHIELD
Datum   conf_one (PG_FUNCTION_ARGS) ;
Datum   pip_atom_conf_sample_g(PG_FUNCTION_ARGS) ;
Datum   pip_atom_sample_set_presence(PG_FUNCTION_ARGS) ;
Datum   pip_set_cdf_sampling_enabled(PG_FUNCTION_ARGS) ;
Datum   pip_atom_in   (PG_FUNCTION_ARGS) ;
Datum   pip_atom_out   (PG_FUNCTION_ARGS) ;
Datum   pip_atom_create_gt_ee   (PG_FUNCTION_ARGS) ;
Datum   pip_atom_create_lt_ee   (PG_FUNCTION_ARGS) ;
Datum   pip_atom_create_gt_ef   (PG_FUNCTION_ARGS) ;
Datum   pip_atom_create_gt_fe   (PG_FUNCTION_ARGS) ;
Datum   pip_atom_create_lt_ef   (PG_FUNCTION_ARGS) ;
Datum   pip_atom_create_lt_fe   (PG_FUNCTION_ARGS) ;
Datum   pip_atomset_in (PG_FUNCTION_ARGS) ;
Datum   pip_atomset_out (PG_FUNCTION_ARGS) ;
Datum pip_conf_tally_in(PG_FUNCTION_ARGS) ;
Datum pip_conf_tally_out(PG_FUNCTION_ARGS) ;
Datum   pip_conf_tally_result(PG_FUNCTION_ARGS) ;
Datum   pip_eqn_in (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_out (PG_FUNCTION_ARGS) ;
Datum   pip_expectation (PG_FUNCTION_ARGS) ;
Datum   pip_expectation_max_g (PG_FUNCTION_ARGS) ;
Datum   pip_expectation_max_g_one (PG_FUNCTION_ARGS) ;
Datum   pip_expectation_max_f_one (PG_FUNCTION_ARGS) ;
Datum   pip_expectation_sum_g (PG_FUNCTION_ARGS) ;
Datum   pip_expectation_sum_g_one (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_sum_ee (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_sum_ei (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_sum_ie (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_sum_ef (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_sum_fe (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_mul_ee (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_mul_ei (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_mul_ie (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_mul_ef (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_mul_fe (PG_FUNCTION_ARGS) ;
Datum  pip_eqn_neg (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_sub_ee (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_sub_ei (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_sub_ie (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_sub_ef (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_sub_fe (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_structural_equals (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_subscript (PG_FUNCTION_ARGS) ;
Datum   pip_eqn_compile_sum (PG_FUNCTION_ARGS) ;
Datum   pip_exp_in (PG_FUNCTION_ARGS) ;
Datum   pip_exp_out (PG_FUNCTION_ARGS) ;
Datum  pip_exp_make (PG_FUNCTION_ARGS) ;
Datum  pip_exp_fix (PG_FUNCTION_ARGS) ;
Datum  pip_exp_expect (PG_FUNCTION_ARGS) ;
Datum   pip_debug_use_sampling_groups   (PG_FUNCTION_ARGS) ;
Datum   pip_presampler_in(PG_FUNCTION_ARGS) ;
Datum   pip_presampler_out(PG_FUNCTION_ARGS) ;
Datum   pip_presampler_create(PG_FUNCTION_ARGS) ;
Datum   pip_presampler_advance(PG_FUNCTION_ARGS) ;
Datum   pip_presampler_get(PG_FUNCTION_ARGS) ;
Datum   pip_sample_set_in(PG_FUNCTION_ARGS) ;
Datum   pip_sample_set_out(PG_FUNCTION_ARGS) ;
Datum   pip_sample_set_generate(PG_FUNCTION_ARGS) ;
Datum   pip_sample_set_explode(PG_FUNCTION_ARGS) ;
Datum   pip_sample_set_expect(PG_FUNCTION_ARGS) ;
Datum   pip_value_bundle_in   (PG_FUNCTION_ARGS) ;
Datum   pip_value_bundle_out  (PG_FUNCTION_ARGS) ;
Datum   pip_value_bundle_create  (PG_FUNCTION_ARGS) ;
Datum   pip_value_bundle_cmp_vv  (PG_FUNCTION_ARGS) ;
Datum   pip_value_bundle_cmp_vi  (PG_FUNCTION_ARGS) ;
Datum   pip_value_bundle_cmp_iv  (PG_FUNCTION_ARGS) ;
Datum   pip_value_bundle_cmp_vf  (PG_FUNCTION_ARGS) ;
Datum   pip_value_bundle_cmp_fv  (PG_FUNCTION_ARGS) ;
Datum   pip_value_bundle_add_vf  (PG_FUNCTION_ARGS) ;
Datum   pip_value_bundle_add_vv  (PG_FUNCTION_ARGS) ;
Datum   pip_value_bundle_mul_vf  (PG_FUNCTION_ARGS) ;
Datum   pip_value_bundle_mul_vv  (PG_FUNCTION_ARGS) ;
Datum   pip_value_bundle_expect  (PG_FUNCTION_ARGS) ;
Datum   pip_value_bundle_max  (PG_FUNCTION_ARGS) ;
Datum   pip_var_in   (PG_FUNCTION_ARGS) ;
Datum   pip_var_out  (PG_FUNCTION_ARGS) ;
Datum   pip_var_create_str (PG_FUNCTION_ARGS) ;
Datum   pip_var_create_row (PG_FUNCTION_ARGS) ;
Datum   pip_world_presence_in   (PG_FUNCTION_ARGS) ;
Datum   pip_world_presence_out  (PG_FUNCTION_ARGS) ;
Datum   pip_world_presence_create(PG_FUNCTION_ARGS) ;
Datum   pip_world_presence_count(PG_FUNCTION_ARGS) ;
Datum   pip_world_presence_union (PG_FUNCTION_ARGS) ;
#endif
